from abc import ABC, abstractmethod
from utils import *

from mininet.node import Node
from typing import Union
from datetime import datetime, timedelta
import json

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
        
        self.cpp_bin_path = "./floodclone/floodclone"  
        self.piece_folder = f"{node.privateDirs[0]}/pieces" 
        
    def start_download(self, *args, **kwargs):
        self.start_time = datetime.now()
        network_info = self.controller.nodes  # This contains your full network topology with interfaces
        
        self.node.cmd(f"mkdir -p {self.piece_folder}")
        
        if self.node == self.src:
            self._start_source(network_info)
        else:
            self._start_destination(network_info)

    def _start_source(self, network_info):
        cmd = (f"{self.cpp_bin_path} "
                f"--mode source "
                f"--node-name {self.node.name} "
                f"--file {self.node.privateDirs[0]}/file "  # Source file location
                f"--pieces-dir {self.piece_folder} "
                f"--network-info '{json.dumps(network_info)}' "
                f"--timestamp-file {self.piece_folder}/completion_time")
        print(f"Executing command: {cmd}")
        self.node.sendCmd(cmd)
        
    def _start_destination(self, network_info):
        cmd = (f"{self.cpp_bin_path} "
                f"--mode destination "
                f"--node-name {self.node.name} "
                f"--src-name {self.src.name} "
                f"--pieces-dir {self.piece_folder} "
                f"--network-info '{json.dumps(network_info)}' "
                f"--timestamp-file {self.piece_folder}/completion_time")
        print(f"Executing command: {cmd}")
        self.node.sendCmd(cmd)
       
    def wait_output_wrapper(self) -> None:
        output = self.node.waitOutput(verbose=VERBOSE)
        print(f"Command output: {output}")
        
        timestamp_str = self.node.cmd(f"cat {self.piece_folder}/completion_time").strip()
        completion_timestamp = datetime.fromtimestamp(float(timestamp_str)/1000000.0)
        self.end_time = completion_timestamp
        