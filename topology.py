from ipaddress import ip_address
from utils import *

from mininet.topo import Topo

class BackboneTopo( Topo ):

    def __init__(self, *args, **kwargs):
        self.pending_routing_updates = []
        self.paths = {}
        if "topology_file" in kwargs and kwargs["topology_file"] is not None:
            Topo.__init__(self, *args, **{k: v for k, v in kwargs.items() if k != "topology_file"})
            self.build_from_file(kwargs["topology_file"])
        else:
            Topo.__init__(self, *args, **kwargs)
            self.build_default()

    @staticmethod
    def get_interface_name(node:str, port: int):
        return f"{node}-eth{port}"

    def build_from_file(self, topology_file):
        # Assumes that the file has links after the rest
        with open(topology_file) as f:
            for line in f:
                parts = line.split()
                if parts[0] == "switch":
                    switch_params = SwitchParams(*parts)
                    self.addSwitch(**switch_params.kwargs_params)
                elif parts[0] == "host":
                    host_params = HostParams(*parts)
                    self.addHost(**host_params.kwargs_params)
                elif parts[0] == "link":
                    link_params = LinkParams(*parts)
                    self.addLink(**link_params.kwargs_params)
                elif parts[0] == "path":
                    path_params = PathParams(parts[1:])
                    self.paths[(path_params.hops[0], path_params.hops[-1])] = path_params.hops[1:-1]
        self.find_missing_routes()

    # When a host has more than 2 connected ports, only the first one results in an automatically generated route.
    # We fix this. This has the assumption that none of the hosts are acting like routers between two networks:
    # otherwise it is missing a couple of Updates.add_route commands
    def find_missing_routes(self):
        hosts = [node for node in self.g.nodes() if not self.isSwitch(node)]
        if len(self.paths) > 0:
            for (node1, node2), hops in self.paths.items():
                port1, port2 = None, None
                for k, v in self.ports[node1].items():
                    if v[0] == hops[0]:
                        port1 = k
                        break
                for k, v in self.ports[node2].items():
                    if v[0] == hops[-1]:
                        port2 = k
                        break
                if port1 is None or port2 is None:
                    raise ValueError("Topology file has path across non-existent link.")
                
                # print(f"{node1}:{port1} - {node2}:{port2}")
                if len(hops) == 1:
                    self.pending_routing_updates.extend(self.set_non_trivial_path(node1, node2, port1, port2))
                else:
                    self.pending_routing_updates.extend(self.set_non_trivial_via_path(node1, node2, hops[1]))
                    self.pending_routing_updates.extend(self.set_non_trivial_via_path(node2, node1, hops[-2]))
        else:
            for h in hosts:
                if len(self.ports[h]) > 1:
                    ports_to_update = {k: v for k, v in self.ports[h].items() if k != 0}
                    for port1, (next_switch, switch_port) in ports_to_update.items():
                        for other_switch_port in self.ports[next_switch]:
                            if other_switch_port == switch_port:
                                continue
                            paired_host, port2 = self.ports[next_switch][other_switch_port]
                            self.pending_routing_updates.extend(BackboneTopo.bring_link_up(h, paired_host, port1, port2, port1 != 0, port2 != 0))
                            self.pending_routing_updates.extend(BackboneTopo.update_other_hosts(hosts, h, paired_host))

    @staticmethod
    def bring_link_up(node1: str, node2: str, port1: int, port2: int, new_ip1: bool, new_ip2: bool):
        res = []
        if new_ip1:
            res.extend([
                (Updates.flush, node1, port1),
                (Updates.add_addr, node1, port1),
                (Updates.ip_forward, node1)
            ])
        if new_ip2:
            res.extend([
                (Updates.flush, node2, port2),
                (Updates.add_addr, node2, port2)
            ])
        res.append((Updates.add_routes, node1, node2, port1, port2))
        return res

    @staticmethod
    def update_other_hosts(hosts: List[str], known_host: str, host_to_be_added: str):
        res = []
        for h in hosts:
            if h != known_host and h != host_to_be_added:
                res.extend([
                    (Updates.add_via_routes, h, host_to_be_added, known_host),
                    (Updates.add_via_routes, host_to_be_added, h, known_host)
                ])
        return res

    @staticmethod
    def set_non_trivial_path(node1: str, node2: str, port1: int, port2: int):
        return [
            (Updates.add_routes, node1, node2, port1, port2),
            (Updates.add_routes, node2, node1, port2, port1),
        ]

    @staticmethod
    def set_non_trivial_via_path(node1: str, node2: str, via: str):
        return [
            (Updates.add_via_routes, node1, node2, via),
            (Updates.ip_forward, via)
        ]

    def build_default(self):
        # Add hosts and switches
        src = self.addHost('src', privateDirs=['/var/mn/source'])
        d1 = self.addHost('d1', privateDirs=['/var/mn/dest1'])
        s1 = self.addSwitch('s1')

        # Add links
        self.addLink(src, s1, bw=100, delay="5ms", max_queue_size=100, cls1=BasicIntf, cls2=BasicIntf, loss=0)
        self.addLink(s1, d1, bw=100, delay="5ms", max_queue_size=100, cls1=BasicIntf, cls2=BasicIntf, loss=0)

    def resolve_pending_routing_updates(self, net):
        
        existing_ips = [ip_address(intf.IP()) for host in net.hosts 
                            for intf in host.intfList() 
                            if intf.name != 'lo' and intf.IP()]
        next_ip = max(existing_ips) + 1 if existing_ips else ip_address('10.0.0.1')
        
        for update in self.pending_routing_updates:
            node = net.get(update[1])
            intf1 = node.intf(BackboneTopo.get_interface_name(node.name, update[3]))
            if update[0] == Updates.flush:
                cmd = f"ip addr flush dev {intf1}"
            elif update[0] == Updates.add_addr:
                cmd = f"ip addr add {next_ip} dev {intf1}"
                next_ip += 1
            elif update[0] == Updates.add_routes:
                node2 = net.get(update[2])
                # Get IPs from specific interfaces using the node and port info
                intf2 = node2.intf(BackboneTopo.get_interface_name(node2.name, update[4]))
                ip1 = intf1.IP()
                ip2 = intf2.IP()
                cmd = f"ip route add {ip2} dev {intf1}"
                cmd2 = f"ip route add {ip1} dev {intf2}"
                node2.cmd(cmd2, verbose=VERBOSE)
                intf2.updateIP()
            elif update[0] == Updates.ip_forward:
                cmd = f"sysctl net.ipv4.ip_forward=1"
            elif update[0] == Updates.add_via_routes:
                _, _, dst, via = update
                dst, via = net.get(dst), net.get(via)
                cmd = f"ip route add {dst.IP()} via {via.IP()}"
            else:
                raise AttributeError(f"Unknown update type {update[0]}")
            node.cmd(cmd, verbose=VERBOSE)
            intf1.updateIP()
