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
The starter code is organized as follows:
 - `topology.py`: Script to read topologies from files, and build them in Mininet. You probably will not need to modify anything here.
 - `topologies/`: Folder containing the topology files to feed into `topology.py`. If you are looking to build your own topology, see the [topology file format](README.md#topology-file-format) section below.
 - `controller.py`: Starts the network, the server on the source, and creates a thread and an `Agent` instance per receiving host. Once every thread completes, reports the final completion time. 
 - `agent.py`: The main place to implement your solution. Each agent should be in charge of any measurements, adjustments to linux options, etc... for its connection with the source. Each agent should fill the self.end_time field **when the download is finished**. We will be checking that there aren't any instructions executed by the agent after it's self.end_time field is set. As a simple example, we provide a `NaiveAgent` that simply calls a command to download the file from the server. 
 - `main.py`: Initializes the controller and reports the final job completion time for the set of all hosts.
 - `utils.py`: sets constants used by multiple files. For debugging purposes, you might want to change the size of the file that gets transferred over the network (`FILE_SIZE`).

The `--mininet-debug` and `--debug` arguments could be useful to understand what is happening. Feel free to also import `mininet.cli.CLI` and call `CLI(net)` to test things.

A sample command to run the starter code would be:
```
sudo python main.py topologies/scenario1.txt --trace-file traces/sc1-trace2.txt
```

## Topology File Format

This is given for reference. Each line defines a network element. The first word of the line defines the type of network element (currently `host`, `switch`, `link`, and `path` are supported). 
 - For hosts, only the name of the host is needed. Lines have the following format: `host <name>`.
 - Same for switches: `switch <name>`.
 - For links, lines have the following format, with all arguments required: `link <node1> <node2> <Bandwidth(Mbps)> <link delay> <max queue size> <loss probability>`.
 - If multiple paths exist between two hosts, the path between each pair of host must be specified, with one pair of hosts per line: `path <node1> <node2> <node3> ...`

## Trace File Format
Traces are given as a list of instructions to change a network element, with a timestamp. For now, the starter code only allows to change properties of links. The format for each instruction is given below:

`<timestamp (ms)> link <key=value> <key=value> ...` for `key, value` valid parameters of `mininet.link.TCIntf.config()`