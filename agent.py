from abc import ABC, abstractmethod
from utils import *

from mininet.node import Node
from typing import Union
from datetime import datetime, timedelta
import json
import time

from utils import FILE_NAME

class Agent(ABC):

    def __init__(self, uid: int, node: Node, src: Node, controller: "controller.Controller", *args, **kwargs) -> None:
        self.uid = uid
        self.node = node
        self.src = src
        self.controller = controller
        self.start_time: Union[datetime, None] = None
        self.end_time: Union[datetime, None] = None

    # This method should first start a timer, then run all the setup you need,
    # including collection of information about the network, and then start the download.
    # For reference, the NaiveAgent below simply downloads the file.
    # It should be clear from reading your code that you start and stop the timing at the right times
    @abstractmethod
    def start_download(self, *args, **kwargs):
        pass

    # This method fills self.jct with the final job completion time
    @abstractmethod
    def wait_output_wrapper(self) -> None:
        pass

    @property
    def jct(self) -> Union[timedelta, None]:
        if self.start_time is None or self.end_time is None:
            return None
        else:
            return self.end_time - self.start_time

class NaiveAgent(Agent):

    def start_download(self, *args, **kwargs):
        self.start_time = datetime.now()
        self.node.sendCmd(f'mkdir -p wget-logs; wget -O {self.node.privateDirs[0]}/file {self.src.IP()}:{PORT}/file')

    def wait_output_wrapper(self) -> None:
        _ = self.node.waitOutput(verbose=VERBOSE)
        self.end_time = datetime.now()


class FloodClone(Agent):
    def __init__(self, uid: int, node: Node, src: Node, controller: "Controller", *args, **kwargs):
        super().__init__(uid, node, src, controller, *args, **kwargs)
        
        self.floodClone_bin = "./floodclone/floodclone"  
        self.piece_folder = f"{node.privateDirs[0]}/pieces" 
        
    def start_download(self, *args, **kwargs):
        # call my c++ interface and start time
        network_info = self.controller.nodes
        ip_map = self.controller.ip_map
        
        # clean up folder from previous runs
        self.node.cmd(f"rm -rf {self.piece_folder} {self.node.name}_completion_time && mkdir -p {self.piece_folder}")
        
        if self.node == self.src:
            cmd = self._get_source_command(network_info, ip_map)
        else:
            cmd = self._get_destination_command(network_info, ip_map)

        # Run command in background, redirect output, and save PID
        print(f"Executing command: {cmd}")
        self.node.cmd(f"{cmd} > {self.node.name}_output.log 2>&1 &")
        self.start_time = datetime.now()

    def _get_source_command(self, network_info, ip_map):
        return (f"{self.floodClone_bin} "
                f"--mode source "
                f"--node-name {self.node.name} "
                f"--file {self.node.privateDirs[0]}/{FILE_NAME} "
                f"--pieces-dir {self.piece_folder} "
                f"--network-info '{json.dumps(network_info)}' "
                f"--ip-map '{json.dumps(ip_map)}' "
                f"--timestamp-file {self.node.name}_completion_time")
        
    def _get_destination_command(self, network_info, ip_map):
        
        if (self.node.name == "d2"):
            return (f"(sleep 1 && {self.floodClone_bin} "
                f"--mode destination "
                f"--node-name {self.node.name} "
                f"--file {self.node.privateDirs[0]}/{FILE_NAME} "
                f"--src-name {self.src.name} "
                f"--pieces-dir {self.piece_folder} "
                f"--network-info '{json.dumps(network_info)}' "
                f"--ip-map '{json.dumps(ip_map)}' "
                f"--timestamp-file {self.node.name}_completion_time)")
        return (f"{self.floodClone_bin} "
                f"--mode destination "
                f"--node-name {self.node.name} "
                f"--file {self.node.privateDirs[0]}/{FILE_NAME} "
                f"--src-name {self.src.name} "
                f"--pieces-dir {self.piece_folder} "
                f"--network-info '{json.dumps(network_info)}' "
                f"--ip-map '{json.dumps(ip_map)}' "
                f"--timestamp-file {self.node.name}_completion_time")
       
    def wait_output_wrapper(self) -> None:
        """
        This is a hacky way of knowing when all the C++ programs are complete. I have attempted 
        multiple solutions but it turns out Mininet has a key limitation - it isn't possible to run 
        two commands using node.cmd at the same time. Since the dynamic_network thread will be using 
        node.cmd/sendCmd to make network changes, we can't use it to check process status. Instead, we look 
        for a completion file that FloodClone writes when it finishes. We check for this file's 
        existence directly from the host OS, avoiding node.cmd entirely until FloodClone is done. 
        The timing isn't affected in any way as that is measured inside the C++ program and written 
        to the completion file. Once we see the file exists, it's safe to use node.cmd to read the 
        timing data since FloodClone has finished.
        """
   
        # Wait for completion file to appear
        import os
        while True:
            if os.path.exists(f"{self.node.name}_completion_time"):
                break
            time.sleep(0.1)
        
        time.sleep(0.2)
        # Now safe to use node.cmd as FloodClone has finished
        with open(f"{self.node.name}_completion_time", 'r') as f:
            timestamp_str = f.read().strip()
        if timestamp_str:
            # print(f"Timestamps for {self.node.name}: {timestamp_str}")
            start_micros, end_micros = map(float, timestamp_str.split())
            self.start_time = datetime.fromtimestamp(start_micros / 1_000_000)
            self.end_time = datetime.fromtimestamp(end_micros / 1_000_000)
        else:
            print(f"Warning: No completion time found for {self.node.name}")