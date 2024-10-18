# FloodClone


# Problem setup

There is a source node, S, containing a src file, F, of size N, and M  destiations nodes (d1, d2, ..., dm) connected
through unkown topologies. The Goal of this project is to deliver the src file to all d nodes.
The efficeincy of the system will be measured by the utility function U = min(T1, T2, T3, T4, ... Tm)
where ti is the time between the di Node starting this application and it recieving the complete file sz.

The goal of this project is to minimize this utility function, by acheiving the smallest amount of download time  for the last node to complete.

lets try to get a better understanding of this utility funciton. In the worst case the source node will have to copy src file to each d node one node at a time. Assuming we didn't use parallization the following topology will force us to do that 

(topology sr connected to each destination with a different link to d destiantions)


in this case the last node will get the complete file after d copies of s which will take a total time of M * N/B, where B is the slowest bandwdith (since we are trying to fidn teh worst time for U). On the other hand if we had a good topology where the S is connected to d1, d1 is connected to d2  and so on, we could divide up the File into x small pieces p1, p2, ... px such that as soon as a node gets a piece pi will start sending it to its neighbour, in thsi apporach all the m links will be utilized and will be n the pipeline after pm leaves the source and gets into the pipelline. not worrying about the small time some of the bandwidthes will be idel, her U = M * N/(B + B + B ... B), the total amount of data being sent is the same as we will have to get F to all teh nodes but we were able to utilize mulitple bandwdith at the same time and thus the total bandwidth availabel went from B to MB, making U = N. whih is way faster than the first case.

(include an image of this topology)


Another intersting case is one where there is a cycle in the network and S is connected directly to all destinations. 

## Project Philosophy

Philosophical Rules:

- **Implementation-Driven Philosophies**:
  1. **Prioritize Faster Receivers**: Always prioritize peers that can receive data faster to reduce overall delivery time.
  2. **Maximize Link Utilization**: Only serve one receiver per link to utilize the link's full bandwidth, avoiding unnecessary sharing that could halve performance.
  3. **Utilize Links Both Ways**: Aim to utilize each link for bidirectional communication to maximize overall efficiency.
  4. **Assume a Dynamic Netowrk**: bandwidth estimation should contionously be revaluated
  5. **Avoid Redundency**:

## Questions and Answers

1. **Can we skip using explicit ping requests and instead measure peer response time by sending the first chunk?**

   Yes, instead of sending separate ping requests, we can measure peer response time by sending the first chunk to all peers and tracking which peer sends back the acknowledgment fastest. With TCP, RTT can be manually estimated by recording the time between sending and receiving the ACK. With QUIC, RTT information is more directly available, which can simplify implementation.
2. **Is RTT information accessible at the application level for TCP and QUIC?**

   In TCP, RTT is not directly available to the application layer, but you can calculate it manually by timing the chunk transfer and the acknowledgment. In QUIC, RTT is exposed to the application layer as part of the protocol's design, making it easier to access and use for peer prioritization.
3. **How should we handle peer selection dynamically as network conditions change?**

   After sending the initial chunk and selecting the fastest peer, continue to monitor RTT for every subsequent chunk transfer. Adjust peer prioritization if a different peer starts performing better, ensuring that the fastest peer at any given time is prioritized for file transfers.
4. **What are the risks of sending the first chunk to all peers simultaneously?**

   Sending the first chunk to all peers at the same time may introduce network congestion, particularly if bandwidth is limited. To mitigate this, consider splitting the initial chunk into smaller parts or staggering the send times slightly to prevent overwhelming the network.
5. **Should we always use constant chunk sizes for Phase One?**

   Yes, during Phase One, use constant chunk sizes to simplify the initial implementation. This makes testing and debugging easier and ensures consistency as other components are being built. Later phases can introduce dynamic chunk sizing based on network conditions.
6. **How can we avoid wasting time when determining the fastest peer?**

   Instead of a separate ping operation, we can utilize the first chunk's transfer as a mechanism to gauge peer speed. This approach provides a practical measurement of actual file transfer performance while also beginning the transfer process immediately.
7. **How do you estimte bandwidth?**

   Use google bbr bandwdith esimtation based on quic
8. **How can we determine which link is connected to which destination given a list of available links and destination IP addresses?**

   - To determine which link is connected to each destination, you can access the kernel's routing table, which contains rules that map network interfaces to specific destinations. On a Linux system, commands like `ip route get <destination IP>` can provide information on the route taken, including which network interface is used. Additionally, tools like `traceroute` can help map the full path to each destination.
9. **Once the destinations start receiving data, do they start sending it to all their connections?**

   - Yes, this is expected and is in line with how we want the system to function. The idea is that each destination (which becomes a seeder) starts forwarding data to other connections as soon as it receives parts of the file. This allows us to leverage the Utilize Links Both Ways philosophy by ensuring that all available bandwidth is used efficiently, including connections that do not pass through the source (src). This mechanism allows for faster dissemination and reduces dependency on the original source for data distribution.
10. **Is it the seeders that find receivers by estimating bandwidth to connections, or is it the receivers that look for seeders (as in torrent-style systems)?**

Pros and Cons:
Seeders Finding Receivers: This approach is good for optimizing bandwidth utilization because seeders can decide which links have the best capacity for sending data. However, it requires the seeder to track the state of each destination—specifically, which chunks each receiver has already received. If seeders don't have an up-to-date state, they might end up sending duplicate packets already in flight or successfully received, wasting network resources. We could mitigate this by implementing a coordinated scheme (e.g., sending even chunks to one peer and odd chunks to another). This way, each receiver is ensured to get different data without the risk of duplication.

Receivers Looking for Seeders: In a typical torrent-style system, receivers request data from available seeders. This allows each receiver to proactively decide which chunks it needs, and it makes use of distributed availability. However, a downside is the need for continuous polling to check which chunks are available and from whom. This can introduce overhead, especially in highly congested networks, slowing down performance.

what if receivers

## Clarification Questions

1. **Is the given bandwidth one-way bandwidth or two-way bandwidth?**
2. **Will destinations know about all their links, and is the process handled per link?**
3. **Do all destination nodes have reach to the source (`src`)?**

   - This question pertains to whether every destination node in the network can directly connect to the source. Depending on the network topology, some destinations might not have a direct link to the source but instead may need to rely on intermediate nodes to receive data.
     https://hilea.dev

## Project Steps Tracking

### Step 1: Define Problem Requirements Clearly

- **Functional Requirements**:

  - Implement a peer-to-peer (P2P) file transfer mechanism between a source and multiple peers.
  - Transfer files by dividing them into fixed-size chunks during Phase One.
  - Use ping-less peer discovery by leveraging the response time from the first chunk transfer to determine the fastest peer.
  - Support basic file encoding and decoding to handle chunking and reassembly at peers.
  - Dynamically reassign peers for chunk transfers based on real-time performance to optimize the overall transfer time.
- **Non-Functional Requirements**:

  - Ensure efficient bandwidth utilization to minimize congestion.
  - Prioritize simplicity in initial implementation for ease of testing and rapid iteration.
  - Design with adaptability in mind, preparing the system for more complex optimizations in later phases.

### Step 2: Create Hypothetical Scenarios to Analyze System Behavior

- **Scenario**: One source and three destinations (D1, D2, D3).
  - **Goal**: Understand how the system handles multiple peers with different network characteristics, including shared and independent paths, by separating individual flows to each network.
  - **Reflection**: How does peer selection impact transfer efficiency? How should implementation philosophies like "Prioritize Faster Receivers" or "Maximize Link Utilization" be adapted or revised?
  - **Implementation Considerations**:
    - **Step 1**: Send initial small chunks to all destinations (`D1`, `D2`, `D3`) to gather RTT and throughput data.
    - **Step 2**: To estimate bandwidth separately for `D1` and `D2`, reduce or pause traffic to one destination at a time. Start by pausing `D2` and measuring bandwidth for `D1`, then switch to `D2` to measure its independent bandwidth.
    - **Step 3**: Use cyclic probing, where each destination (`D1` and `D2`) is prioritized for a set duration, to maintain up-to-date bandwidth estimates.
    - **Step 4**: Dynamically allocate resources based on the observed throughput, adjusting in real-time as network conditions change.
    - **Step 5**: Evaluate `D3` on its independent link and compare its throughput with `D1` and `D2` to determine the most efficient data transfer path.
  - **Goal**: Understand how the system handles multiple peers with different network characteristics.
  - **Reflection**: How does peer selection impact transfer efficiency? How should implementation philosophies like "Prioritize Faster Receivers" or "Maximize Link Utilization" be adapted or revised?
  - **Implementation Considerations**:
    - Start by sending a chunk to each destination and evaluate response times.
    - Ensure that the link utilization rule is followed: one receiver per link and using bidirectional links when possible.
    - Revise the peer selection mechanism if D1 consistently underperforms, considering fallback mechanisms or HTTP fallback as a possible later phase feature.

Sender receiver problem
problem Articulation:

    Context:
        In a peer-to-peer network, nodes (destinations) request chunks of a file from seeders or other peers.
        You want to avoid redundancy and make sure that each sender distributes data in a way that maximizes efficiency, i.e., by prioritizing nodes that have higher bandwidth and can contribute effectively to the redistribution process.

    The Problem:
        If the receiving nodes (destinations) are the ones that decide when to request a chunk (using RREQ messages), we might end up in situations where:
            A node with lower bandwidth requests a chunk first, and the sender starts sending to it.
            Later, a more optimal node (with higher bandwidth) makes a request, but the sender is now busy sending to the less efficient node.
        This creates a situation where the network efficiency is not optimized. The sender might be engaged in a less productive transfer, which delays the overall data dissemination and reduces throughput.
        Essentially, the decision of whom to send data to should be made by the sender to prioritize efficiency, but key information about which nodes need which chunks and which packets are already in-flight remains with the receiving nodes.
        Thus, we face a synchronization challenge: The sender needs to know:
            Which nodes are requesting specific chunks.
            Which nodes would be optimal to send to (based on available bandwidth).
            Which packets are in-flight to avoid redundant transmissions.

Articulated Problem Statement:

    How can a sender make optimal, efficient decisions about which nodes to send chunks to when the critical information needed for decision-making (i.e., the needs of the nodes and bandwidth conditions) is distributed among the nodes themselves? This is a synchronization problem, where information about needs is distributed, but decision-making is centralized.
