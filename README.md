# Content Delivery Contest
##### MIT - 6.S042/6.5820 Fall 2024

This is the starter code for the final project of 6.S042. Please refer to the Project PDF on Canvas for a high level overview of the problem.

## Setup
You can use the same VM that you used for the PSets and need no further setup.

You might run at some point into storage issues if the files being transferred are too big, or if there are too many destination hosts in the topology. In this case, you can increase the size of the VM in its options.

If you want to create a new VM, please use the same settings as for PSet 1, but in the menu where you change the OS to Ubuntu, increase the Size(GB) parameter to a bigger value. Please also run 
```
sudo ./setup.sh
```
to setup mininet again on your new VM.

/!\ This will increase the cost of the VM, so it is even more important to make sure you stop your instance when you are not using it. We only have limited extra Google Cloud credits, so please be mindful of your utilization, and contact the TAs if you run into issues.

## Starter Code

The code should be run using the following command:
```
sudo python main.py <path to topology file> [optional --trace-file <path to trace file>] [optional --debug] [optional --mininet-debug]
```
A sample command to run the starter code would be:
```
sudo python main.py topologies/scenario1.txt --trace-file traces/sc1-trace2.txt
```

There are two loggers that you can use: one specific to this project that is controlled through the `--debug` argument, and the mininet one that you can control through `--mininet-debug`. Feel free to use either or both to understand what is happening. Feel free to also import `mininet.cli.CLI` and call `CLI(net)` anywhere in your code to test things.

The starter code is organized as follows:
 - `topology.py`: Script to read topologies from files, and build them in Mininet. You probably will not need to modify anything here.
 - `topologies/`: Folder containing the topology files to feed into `topology.py`. If you are looking to build your own topology, see the [topology file format](README.md#topology-file-format) section below.
 - `traces/`: Folder containing network traces, passed with the `--trace-file` argument. The naming scheme for traces is `sc<scenario#>-trace<trace#>.txt`, but feel free to use which ever other scheme if it makes more sense to you. The format for these files is specified in the [trace file format](README.md#trace-file-format) below. The starter code only includes two basic traces for scenario 1. We might release new traces for you to test with, and we will definitely test your solutions using different traces. Feel free to try and come up with traces that you think might stress your solution to test it.
 - `controller.py`: Starts the network, the server on the source, and creates a thread and an `Agent` instance per receiving host. Once every thread completes, reports the final completion time. 
 - `agent.py`: The main place to implement your solution. Each agent should be in charge of any measurements, adjustments to linux options, etc... for its connection with the source. Each agent should fill the self.end_time field **when the download is finished**. We will be checking that there aren't any instructions executed by the agent after it's self.end_time field is set. As a simple example, we provide a `NaiveAgent` that simply calls a command to download the file from the server. 
 - `main.py`: Initializes the controller and reports the final job completion time for the set of all hosts.
 - `utils.py`: sets constants used by multiple files. For debugging purposes, you might want to change the size of the file that gets transferred over the network (`FILE_SIZE`).

You are free to modify anything, but keep in mind that most of your work should be the implementation of an agent. We will only check that your code runs in our evaluation scenario, and we will check that you account for time properly when downloading the file.

## Topology File Format

This is given for reference. Each line defines a network element. The first word of the line defines the type of network element (currently `host`, `switch`, `link`, and `path` are supported). 
 - For hosts, only the name of the host is needed. Lines have the following format: `host <name>`.
 - Same for switches: `switch <name>`.
 - For links, lines have the following format, with all arguments required: `link <node1> <node2> <Bandwidth (Mbit/s)> <link delay (e.g. "5ms")> <max queue size (in packets)> <loss probability>`.
 - If multiple paths exist between two hosts, the path between each pair of host must be specified, with one pair of hosts per line: `path <node-1> <node-2> <node-3> ... <node-n>`. `<node-1>` and `<node-n>` are the source and destination hosts.

Feel free to look at the provided topology files for reference.

## Trace File Format
Traces are given as a list of instructions to change a network element, with a timestamp. For now, the starter code only allows to change properties of links. The format for each instruction is given below:
```
<timestamp (ms)> link <key1=value1> <key2=value2> ...`
```
For `key-n, value-n` valid parameters of `mininet.link.TCIntf.config()` include:
 - `bw`: bandwidth in Mbit/s
 - `delay`: transmission delay with its unit (_e.g._ `"5ms"`)
 - `jitter`: link jitter (_e.g._ `"1ms"`)
 - `loss`: Probability of packet loss on this link
 - `max_queue_size`: Size of the switch buffer, in packets

The traces assume that the timestamps are strictly increasing. Feel free to look at the provided trace files for reference.