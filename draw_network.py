import networkx as nx
import matplotlib.pyplot as plt
import random
import os

class NetworkTopology:
    def __init__(self):
        self.G = nx.Graph()
        self.hosts = []
        self.switches = []
        self.paths = []

    def load_topology(self, topology_file):
        # Read the topology file and add nodes and edges to the graph
        with open(topology_file, 'r') as file:
            for line in file:
                parts = line.split()

                if parts[0] == 'host':
                    host_name = parts[1]
                    self.hosts.append(host_name)
                    self.G.add_node(host_name, type='host')

                elif parts[0] == 'switch':
                    switch_name = parts[1]
                    self.switches.append(switch_name)
                    self.G.add_node(switch_name, type='edge_switch')

                elif parts[0] == 'link':
                    node1 = parts[1]
                    node2 = parts[2]
                    bandwidth = parts[3] + "Mbps"
                    delay = parts[4]
                    self.G.add_edge(node1, node2, bandwidth=bandwidth, delay=delay)

                elif parts[0] == 'path':
                    self.paths.append(parts[1:])

    def store_topology(self, filename):
        # Save the topology to a text file in the same format as the input
        with open(filename, 'w') as file:
            for host in self.hosts:
                file.write(f'host {host}\n')
            for switch in self.switches:
                file.write(f'switch {switch}\n')
            for u, v, d in self.G.edges(data=True):
                file.write(f'link {u} {v} {d["bandwidth"][:-4]} {d["delay"]}\n')
            for path in self.paths:
                file.write(f'path {" ".join(path)}\n')

    def generate_image(self, filename):
        # Set dynamic figure size based on the number of nodes
        num_nodes = len(self.G.nodes)
        plt.figure(figsize=(num_nodes * 1.5, num_nodes * 1.5))

        # Get positions for the nodes in the network
        pos = nx.spring_layout(self.G, k=15, iterations=100)

        # Customization for nodes
        src_node = [n for n in self.G.nodes if n == 'src']  # Isolate the src node
        host_nodes = [n for n, d in self.G.nodes(data=True) if d['type'] == 'host' and n != 'src']  # Exclude src from the hosts
        other_host = [n for n, d in self.G.nodes(data=True) if d['type'] == 'other_host']
        edge_switch_nodes = [n for n, d in self.G.nodes(data=True) if d['type'] == 'edge_switch']
        backbone_switch = [n for n, d in self.G.nodes(data=True) if d['type'] == 'backbone_switch']

        # Draw the src node in red
        nx.draw_networkx_nodes(self.G, pos, nodelist=src_node, node_shape='o', node_color='yellow', node_size=2000, label=True)
        nx.draw_networkx_nodes(self.G, pos, nodelist=host_nodes, node_shape='o', node_color='lightgreen', node_size=2000, label=True)
        nx.draw_networkx_nodes(self.G, pos, nodelist=other_host, node_shape='o', node_color='green', node_size=2000, label=True)
        nx.draw_networkx_nodes(self.G, pos, nodelist=edge_switch_nodes, node_shape='s', node_color='skyblue', node_size=4000, label=True)
        nx.draw_networkx_nodes(self.G, pos, nodelist=backbone_switch, node_shape='s', node_color='blue', node_size=4000, label=True)
        nx.draw_networkx_edges(self.G, pos, width=2, style='solid', arrows=False)

        # Add labels to the nodes
        nx.draw_networkx_labels(self.G, pos, font_size=12, font_color='black')

        # Add edge labels (bandwidth, delay)
        edge_labels = {(u, v): f'{d["bandwidth"]}, {d["delay"]}' for u, v, d in self.G.edges(data=True)}
        nx.draw_networkx_edge_labels(self.G, pos, edge_labels=edge_labels, font_size=10, label_pos=0.5, rotate=False)

        plt.title("Network Topology with Bandwidth and Delay")
        plt.axis('off')
        plt.savefig(filename)
        plt.close()
    
    

class NetworkTopologyGenerator:
    def __init__(self, output_folder='topologies'):
        self.output_folder = output_folder
        self.topology = NetworkTopology()

    def generate_topology(self, num_hosts, num_switches, redundancy='minimal', other_hosts=0, num_backbone_switchs=0):
        # Reset the graph
        self.topology.G.clear()
        self.topology.hosts = []
        self.topology.switches = []
        self.topology.paths = []

        # Create hosts and switches
        for i in range(num_hosts):
            host_name = 'src' if i == 0 else f'd{i}'
            self.topology.hosts.append(host_name)
            self.topology.G.add_node(host_name, type='host')
        
        if num_switches < 2:
            num_backbone_switchs = 0
            redundancy = "minimal"

        
        edge_switch_count = max(1, num_switches - num_backbone_switchs)

        # Create edge switches (connects to hosts)
        for i in range(edge_switch_count):
            switch_name = f's{i + 1}'
            self.topology.switches.append(switch_name)
            self.topology.G.add_node(switch_name, type='edge_switch')

        edge_switches = [sw for sw in self.topology.switches if self.topology.G.nodes[sw]['type'] == 'edge_switch']
        # Randomly connect hosts to switches
        for host in self.topology.hosts:
            switch = random.choice(edge_switches)
            bandwidth = f'{random.randint(10, 1000)}Mbps'
            delay = f'{random.randint(1, 50)}ms'
            self.topology.G.add_edge(host, switch, bandwidth=bandwidth, delay=delay)

            # Add additional connections for medium or high redundancy
            if redundancy in ['medium', 'high']:
                additional_switches = random.sample(self.topology.switches, k=min(len(self.topology.switches), 2) if redundancy == 'medium' else len(self.topology.switches) - 1)
                for additional_switch in additional_switches:
                    if additional_switch != switch and not self.topology.G.has_edge(host, additional_switch):
                        bandwidth = f'{random.randint(10, 1000)}Mbps'
                        delay = f'{random.randint(1, 50)}ms'
                        self.topology.G.add_edge(host, additional_switch, bandwidth=bandwidth, delay=delay)


        # Create backbone switches (connects to other switches but not hosts)
        for i in range(num_backbone_switchs):
            switch_name = f'bs{i + 1}'
            self.topology.switches.append(switch_name)
            self.topology.G.add_node(switch_name, type='backbone_switch')

        # Ensure all switches are connected by creating a minimum spanning tree (MST)
        switch_subgraph = self.topology.G.subgraph([sw for sw in self.topology.switches if self.topology.G.nodes[sw]['type'] in ['edge_switch', 'backbone_switch']])
        mst = nx.minimum_spanning_tree(switch_subgraph)
        for u, v in mst.edges():
            if not self.topology.G.has_edge(u, v):
                bandwidth = f'{random.randint(10, 1000)}Mbps'
                delay = f'{random.randint(1, 50)}ms'
                self.topology.G.add_edge(u, v, bandwidth=bandwidth, delay=delay)

        # Add additional connections for redundancy
        num_connections = max(1, num_switches * 2) if redundancy == 'minimal' else num_switches * 3 if redundancy == 'medium' else num_switches * 4
        for _ in range(num_connections):
            if len(self.topology.switches) > 1:
                switch1, switch2 = random.sample([sw for sw in self.topology.switches if self.topology.G.nodes[sw]['type'] in ['edge_switch', 'backbone_switch']], 2)
            else:
                continue
            if not self.topology.G.has_edge(switch1, switch2):
                bandwidth = f'{random.randint(10, 1000)}Mbps'
                delay = f'{random.randint(1, 50)}ms'
                self.topology.G.add_edge(switch1, switch2, bandwidth=bandwidth, delay=delay)
        
        # Add other hosts (with minimal connection logic)
        for i in range(other_hosts):
            other_host_name = f'oh{i + 1}'
            self.topology.G.add_node(other_host_name, type='other_host')
            
            # Connect each additional host to a random edge switch (minimal connection)
            switch = random.choice([sw for sw in self.topology.switches if self.topology.G.nodes[sw]['type'] == 'edge_switch'])
            bandwidth = f'{random.randint(10, 100)}Mbps'
            delay = f'{random.randint(5, 50)}ms'
            self.topology.G.add_edge(other_host_name, switch, bandwidth=bandwidth, delay=delay)

        # Generate all paths between hosts (including direct routes)
        for i in range(len(self.topology.hosts) - 1):
            for j in range(i + 1, len(self.topology.hosts)):
                src = self.topology.hosts[i]
                dst = self.topology.hosts[j]
                all_paths = list(nx.all_simple_paths(self.topology.G, source=src, target=dst))
                for path in all_paths:
                    self.topology.paths.append(path)
        
    
    def store(self, name, image=True):
        self.topology.generate_image(f'{name}.png')
        self.topology.store_topology(f'{name}.txt')

# Example usage:
if __name__ == "__main__":
    generator = NetworkTopologyGenerator()
    redundancy_levels = ['minimal', 'medium', 'high']  
    #  num_hosts, num_switches, redundancy='minimal', other_host=0, backbone_probability=0.3
  
    # Generate comprehensive scenarios
    i = 6
    for num_hosts in range(2, 13, 2):  # Hosts from 2 to max_hosts in steps of 2
        for num_switches in range(1, num_hosts // 2 + 2):
            red = redundancy_levels[:min(num_switches, 3)]
            for redundancy in red:  
                for other_host in range(0, num_hosts + 2):
                    
                    if num_switches < 3:
                        generator.generate_topology(num_hosts, num_switches, redundancy, other_host, 0)
                        generator.store(f"topologies/scenario{i}")
                        i += 1
                    
                    else:            
                        for bbs in range(0, num_switches - 1, 2):    
                            generator.generate_topology(num_hosts, num_switches, redundancy, other_host, bbs)
                            generator.store(f"topologies/scenario{i}")
                            i += 1
                        
