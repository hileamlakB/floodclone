from datetime import timedelta
from enum import Enum, auto
from dataclasses import dataclass
from typing import List
from threading import Thread, Event
import re
from mininet.link import Intf, TCIntf

VERBOSE = True
PORT = 8000
FILE_SIZE = "1M"
FILE_NAME = "file"

server_pid_re_psAux = re.compile("root +(?P<server_pid>[0-9]+).+ python -m http.server --directory /var/mn/src/ --bind [0-9. ]+")
md5_re = re.compile("(?P<md5>[0-9a-zA-Z]+) +/var/mn/(?P<node>[a-z0-9A-Z]+)/file")

class Updates(Enum):
    flush = auto()
    add_addr = auto()
    ip_forward = auto()
    add_routes = auto()
    add_via_routes = auto()

# https://stackoverflow.com/questions/323972/is-there-any-way-to-kill-a-thread
class StoppableThread(Thread):
    """Thread class with a stop() method. The thread itself has to check
    regularly for the stopped() condition."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._stop_event = Event()

    def stop(self):
        self._stop_event.set()

    @property
    def stopped(self):
        return self._stop_event.is_set()

@dataclass
class TraceEvent:
    timestamp: float
    event: List[str]

    def __post_init__(self):
        self.tdTimestamp = timedelta(milliseconds=float(self.timestamp))
        self.event_type = self.event[0]
        if self.event_type == "link":
            d = {e.split("=")[0]: e.split("=")[1] for e in self.event[1:]}
            self.event_data = PartialLinkParams(**d)
        else:
            raise ValueError(f"Unknown event type: {self.event_type}")

class BasicIntf(TCIntf):
  """An interface with TSO and GSO disabled."""

  def config(self, **params):
    result = super(BasicIntf, self).config(**params)

    self.cmd('ethtool -K %s tso off gso off' % self, verbose=VERBOSE)

    return result

@dataclass
class HostParams:
    element_type: str
    name: str

    def __post_init__(self):
        self.privateDirs: str = f"/var/mn/{self.name}"

    @property
    def kwargs_params(self):
        return {"name": self.name, "privateDirs": [self.privateDirs]}

    def __hash__(self):
        return hash(self.name)

@dataclass
class SwitchParams:
    element_type: str
    name: str

    @property
    def kwargs_params(self):
        return {"name": self.name}

    def __hash__(self):
        return hash(self.name)

@dataclass
class LinkParams:
    element_type: str
    node1: str
    node2: str
    bw: float
    delay: str
    max_queue_size: int
    loss: float
    link_number: int = 0
    intf: Intf = BasicIntf

    def __post_init__(self):
        self.bw = float(self.bw)
        self.max_queue_size = int(self.max_queue_size)
        self.loss = float(self.loss)

    @property
    def kwargs_params(self):
        d = {k: v for k, v in self.__dict__.items() if k != 'element_type' and k != "link_number"}
        return d

@dataclass
class PartialLinkParams:
    node1: str
    node2: str
    bw: float = None
    delay: str = None
    max_queue_size: int = None
    loss: float = None
    link_number: int = 0

    def __post_init__(self):
        if self.bw is not None:
            self.bw = float(self.bw)
        if self.max_queue_size is not None:
            self.max_queue_size = int(self.max_queue_size)
        if self.loss is not None:
            self.loss = float(self.loss)

    @property
    def different_link_params(self):
        return {k: v for k, v in self.__dict__.items() if k != "link_number" and "node" not in k and v is not None}

@dataclass
class PathParams:
    hops: List[str]
