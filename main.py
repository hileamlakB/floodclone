from agent import NaiveAgent
from controller import Controller
from argparse import ArgumentParser

from mininet.log import lg
from mininet.cli import CLI
import logging
import sys

if __name__ == '__main__':
    parser = ArgumentParser()
    parser.add_argument("topology", help="Topology file")
    parser.add_argument("--trace-file", help="Trace file", default=None)
    parser.add_argument('--debug', help="Set logging level to debug", action="store_const", dest="loglevel", const=logging.DEBUG, default=logging.INFO)
    parser.add_argument('--warning', help="Set logging level to info", action="store_const", dest="loglevel", const=logging.WARNING)
    parser.add_argument('--mininet-debug', help="Set mininet logging to debug", action="store_true")
    args = parser.parse_args()

    mininet_handler = logging.StreamHandler(sys.stdout)
    mininet_handler.setFormatter(logging.Formatter("%(message)s"))
    lg.addHandler(mininet_handler)
    lg.setLevel("INFO" if args.mininet_debug else "WARNING") # Given all the cmd commands are called with verbose=True, this will print all commands executed on servers

    handler = logging.StreamHandler(sys.stdout)
    handler.setFormatter(logging.Formatter("%(message)s"))
    logger = logging.getLogger("Project")
    logger.addHandler(handler)
    logger.setLevel(args.loglevel)
    #TODO: Change the agent class to your custom agent
    ctrl = Controller(args, NaiveAgent)
    ctrl.start_agents()
    ctrl.join_agents()
    ctrl.check_all_md5()

    ctrl.tear_down_network()
    ctrl.debug_log_all_dl_times()
    logger.info(f"Worse download time: {ctrl.final_jct}")

# sudo python main.py topologies/scenario1.txt --trace-file traces/sc1-trace2.txt