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
from mininet.cli import CLI
from topology import BackboneTopo
from typing import List, Type, Union

from collections import defaultdict

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
        self.nodes, self.ip_map = self._gather_dests() # will store infromatin about destination nodes and hopcounts to them
    
    def _get_ip_aliases(self):
        """
        Retrieves all IP addresses (including secondary IPs) for each interface on each node.
        Returns a dictionary with node names as keys and a list of (interface, IP) tuples as values.
        
        This calculation should not be included in the protocol download time. Instead, it can be viewed 
        as a prerequisite for a node to present its interfaces and all available IPs to join the swarm. This 
        setup constitutes part of the dynamic information necessary for effective network participation. 
        """
        ip_aliases = defaultdict(list)

        for node in self.net.hosts:
            ip_output = node.cmd("ip addr show").splitlines()
            current_intf = None

            for line in ip_output:
                # Detect IP and interface in "inet" lines
                if "inet " in line:
                    parts = line.split()
                    ip = parts[1].split("/")[0]  # Extract the IP address, discard CIDR
                    interface_name = parts[-1]  # Extract the interface name at the end of the line
                    if interface_name != "lo":  # Skip loopback interface
                        ip_aliases[node.name].append((interface_name, ip))
        
        return ip_aliases
    
    def _gather_dests(self):
        """
        Gather node information, storing hop counts and path details between every node pair in a dictionary of dictionaries.
        Each entry is {node_name: {other_node_name: [(connection_interface, hop_count, [list of hops])]}}.
        """
        node_paths = defaultdict(dict)

        node_info = self._get_ip_aliases()

        # Create an inverse mapping of IPs to node names
        node_infoInv = {ip: name for name, intf_ips in node_info.items() for _, ip in intf_ips}

        for node in self.net.hosts:
            for other_node in self.net.hosts:
                if node == other_node:
                    continue  # Skip self-pairs

                interfaces = []
                paths = []

                # Loop over each IP of other_node
                for _, other_ip in node_info[other_node.name]:
                    # Determine interfaces used from node to each IP of other_node
                    route_info = node.cmd(f"ip route get {other_ip}")
                    self.logger.debug(f"Route Info for {node.name} to {other_node.name} at {other_ip}: {route_info}")

                    for line in route_info.splitlines():
                        if "dev" in line:
                            interface = line.split("dev")[1].split()[0]
                            if interface not in interfaces:
                                interfaces.append(interface)
                    self.logger.debug(f"Interfaces used from {node.name} to {other_node.name}: {interfaces}")

                    # Run mtr for each interface to determine hop count and path
                    for iface in interfaces:
                        mtr_output = node.cmd(f"mtr -n -c 1 -r -I {iface} {other_ip}")
                        self.logger.debug(f"MTR Output for {node.name} to {other_node.name} via {iface}:\n{mtr_output}")

                        hop_count = len(mtr_output.splitlines()) - 2
                        path = [
                            node_infoInv.get(line.split()[1], line.split()[1])  # Use node name if available, else keep IP
                            for line in mtr_output.splitlines()[2:]
                        ]

                        paths.append((iface, hop_count, path))

                # Store paths in only one direction
                node_paths[node.name][other_node.name] = paths
                self.logger.debug(f"Node Paths for {node.name} to {other_node.name}: {node_paths[node.name][other_node.name]}")

        self.logger.info(f"Final Node Paths:\n{node_paths}")
        return node_paths, node_info

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
        # self.src.cmd(f"kill -9 {self.server_pid}", verbose=VERBOSE)
        self.net.stop()

    # modified this becuase i am not doing a client server design 
    # instead I am doing a peer to peer network
    def create_file_and_start_server(self):
        self.src.cmd(f"head -c {FILE_SIZE} </dev/urandom >{self.src.privateDirs[0]}/{FILE_NAME}", verbose=VERBOSE)
        self.src.cmd("ls /var/mn/src", verbose=VERBOSE)
        self.src.cmd(f"chmod 777 {self.src.privateDirs[0]}/{FILE_NAME}", verbose=VERBOSE)
        # self.src.cmd(f"python -m http.server --directory {self.src.privateDirs[0]}/ --bind {self.src.IP()} {PORT} &", verbose=VERBOSE)
        
        md5sum = self.poll_cmd(self.src, f"md5sum {self.src.privateDirs[0]}/{FILE_NAME}", md5_re, "md5")
        return md5sum, None

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
                print(f"Node1 type: {type(node1)}, Node2 type: {type(node2)}")
                link = self.net.linksBetween(node1, node2)[event.event_data.link_number]
                print("node:", node1, node2)
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
        
        # Add source node as an agent
        # I have removed the server setup that was done inside
        # write server
        self.agents.append(self.agent_class(0, self.src, self.src, self))
        self.agents[-1].start_time = datetime.now()
        self.agents[-1].start_download()
        
       
        
        self.network_conditions_thread.start()
        # Add destination nodes
        for i, dest in enumerate(self.dests):
            self.agents.append(self.agent_class(i+1, dest, self.src, self))
            self.agents[-1].start_time = datetime.now()
            self.agents[-1].start_download()
        self.logger.info("Started all downloads")

    def join_agents(self):
        # I made this modfification since now src is being treated as a normal peer and nothing 
        # different
        for agent in self.agents:  # Loop through all agents including source
            thread = Thread(target=agent.wait_output_wrapper, args=())
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

