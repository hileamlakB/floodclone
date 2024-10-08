from abc import ABC, abstractmethod
from utils import *

from mininet.node import Node
from typing import Union
from datetime import datetime, timedelta

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

#TODO: Add your custom agent class
