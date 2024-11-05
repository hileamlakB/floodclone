import logging
from argparse import Namespace

from agent import Agent
from datetime import datetime
from utils import *
from time import sleep
import networkx as nx

from mininet.node import CPULimitedHost, Switch, Host
from mininet.net import Mininet, Link
from mininet.util import dumpNodeConnections
from topology import BackboneTopo
from typing import List, Type, Union

class Controller:
    def __init__(self, cli_args: Namespace, agent_class: Type[Agent]):
        self.agents: List[Agent] = []
        self.agent_threads: List[Thread] = []
        self.agent_class = agent_class
        self.logger = logging.getLogger("Project")
        self.topology_file = cli_args.topology
        self.net, self.src, self.dests, self.topo = self.init_network()
        self.md5, self.server_pid = self.create_file_and_start_server()
        self.trace_file = cli_args.trace_file
        if self.trace_file is not None:
            self.network_events = self.parse_trace_file()
        else:
            self.network_events = []
        self.network_conditions_thread = StoppableThread(target=self.dynamic_network, args=())


    def create_net(self):
        topo = BackboneTopo(topology_file=self.topology_file)
        return Mininet(topo=topo, host=CPULimitedHost, link=Link)

    def init_network(self):
        net = self.create_net()
        net.start()
        dumpNodeConnections(net.hosts)
        net.topo.resolve_pending_routing_updates(net)
        src = net.get("src")
        return net, src, [d for d in net.hosts if d != src], net.topo.g.convertTo(nx.MultiGraph)

    def traceroute(self, from_node: Host, to_node: Host):
        return [self.net.get(n) for n in nx.shortest_path(self.topo, from_node.name, to_node.name)[1:] if isinstance(self.net.get(n), Host)]

    def tear_down_network(self):
        self.src.cmd(f"kill -9 {self.server_pid}", verbose=VERBOSE)
        self.net.stop()

    def create_file_and_start_server(self):
        self.src.cmd(f"head -c {FILE_SIZE} </dev/urandom >{self.src.privateDirs[0]}/file", verbose=VERBOSE)
        self.src.cmd("ls /var/mn/src", verbose=VERBOSE)
        self.src.cmd("chmod 777 /var/mn/src/file", verbose=VERBOSE)
        self.src.cmd(f"python -m http.server --directory {self.src.privateDirs[0]}/ --bind {self.src.IP()} {PORT} &", verbose=VERBOSE)
        sleep(2)
        server_pid = self.poll_cmd(self.src, "ps aux | grep http.server", server_pid_re_psAux, "server_pid")
        return self.poll_cmd(self.src, f"md5sum {self.src.privateDirs[0]}/file", md5_re, "md5"), server_pid

    # Sometimes other commands bleed over. Sending a bunch of md5 commands to be sure we get what we want
    @staticmethod
    def poll_cmd(node, command, regex, group_name, polling_number=10, none_ok=False):
        value = None
        for i in range(polling_number):
            if value is None:
                output = node.cmd(command, verbose=VERBOSE)
                for line in output.splitlines():
                    m = regex.match(line)
                    if m is not None:
                        value = m.group(group_name)
        if value is None and not none_ok:
            raise ValueError(f"Could not get parsable output for command: {command}.")
        return value


    def parse_trace_file(self):
        events = []
        with open(self.trace_file) as f:
            for line in f:
                parts = line.split()
                events.append(TraceEvent(parts[0], parts[1:]))
        return events

    def dynamic_network(self):
        self.logger.debug(self.network_events)
        if len(self.network_events) == 0:
            return
        experiment_start = datetime.now()
        sleep(max(self.network_events[0].tdTimestamp.total_seconds(), 0))
        for i, event in enumerate(self.network_events):
            if event.event_type == "link":
                node1 = self.net.get(event.event_data.node1)
                node2 = self.net.get(event.event_data.node2)
                link = self.net.linksBetween(node1, node2)[event.event_data.link_number]
                if isinstance(node1, Switch):
                    link.intf1.config(**event.event_data.different_link_params)
                elif isinstance(node2, Switch):
                    link.intf2.config(**event.event_data.different_link_params)
                else:
                    raise ValueError(f"Invalid topology: two hosts ({node1.name}, {node2.name}) are connected together")
            if self.network_conditions_thread.stopped:
                break
            if i < len(self.network_events) - 1:
                sleep(max((experiment_start + self.network_events[i+1].tdTimestamp - datetime.now()).total_seconds(), 0))

    def start_agents(self):
        self.network_conditions_thread.start()
        for i, dest in enumerate(self.dests):
            self.agents.append(self.agent_class(i, dest, self.src, self))
            self.agents[-1].start_time = datetime.now()
            self.agents[-1].start_download()
        self.logger.info("Started all downloads")

    def join_agents(self):
        for i, dest in enumerate(self.dests):
            thread = Thread(target=self.agents[i].wait_output_wrapper, args=())
            thread.start()
            self.agent_threads.append(thread)
        for agent_thread in self.agent_threads:
            agent_thread.join()
        self.logger.info("All hosts finished downloading")
        self.network_conditions_thread.stop()

    @property
    def final_jct(self) -> Union[timedelta, None]:
        if any(a.jct is None for a in self.agents) or len(self.agents) == 0:
            return None
        return max(a.jct for a in self.agents)

    def debug_log_all_dl_times(self):
        self.logger.debug("Per host download time:")
        for a in self.agents:
            self.logger.debug(f"\t{a.node.name}: {a.jct}")

    def check_all_md5(self):
        md5s = {dest.name: self.poll_cmd(dest, f"md5sum {dest.privateDirs[0]}/file", md5_re, "md5") for dest in self.dests}
        self.logger.debug(f"All md5sums: {md5s}")
        assert all(md5s[dest.name] == self.md5 for dest in self.dests), "Some files were not downloaded properly."

