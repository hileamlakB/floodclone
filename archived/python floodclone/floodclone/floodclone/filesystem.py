import os
import hashlib
import bencodepy
from typing import List, Dict
import tempfile
import logging
import math
import ipaddress 

class FileManager:
    def __init__(self, file_path: str = None, piece_size: int = 0, file_metadata: dict = None, src_ip: str = None, pieces_folder: str = None, logger: logging.Logger = None):
        """
        Initialize FileManager with optional file path, piece size, and pieces folder.
        If no file_path is provided, it acts as a receiver node handler, and expects
        a metadata to be passed
        """
        
        self.logger = logger or logging.getLogger("null")
        if isinstance(self.logger, logging.Logger):
            self.logger.setLevel(logging.DEBUG)


        self.file_path = file_path
        self.piece_size = piece_size if piece_size > 0 else 16384
        self.file_metadata = file_metadata if file_metadata else {}
        
        self._verify_ip(src_ip)
        self.src_ip = src_ip
        
        

        self.pieces_folder = pieces_folder or tempfile.mkdtemp(prefix="floodclone_pieces_")
        
        self._available_pieces = set()
        self._missing_pieces = set()

        
        if self.file_metadata:
            # loading from metadata
            self.file_id = self.file_metadata[b'file_id'].decode('utf-8')
            self.filename = self.file_metadata[b'filename'].decode('utf-8')
            self.file_dir = os.path.join(self.pieces_folder, f"dir_{self.file_id}")
            self.num_pieces = self.file_metadata[b'num_pieces']
        else:
            # using file name as a hash
            # check if the file exists
            assert(os.path.exists(self.file_path))
            self.file_id = hashlib.sha1(self.file_path.encode()).hexdigest()
            self.filename = os.path.basename(self.file_path)
            self.file_dir = os.path.join(self.pieces_folder, f"dir_{self.file_id}")
            file_size = os.path.getsize(self.file_path)
            self.num_pieces = math.ceil(file_size / self.piece_size)
            
            # create metadat
            self.file_metadata = {
                b'file_id': self.file_id,
                b'filename': self.filename,
                b'src_ip': self.src_ip,  # Placeholder for actual source IP
                b'num_pieces': self.num_pieces,
                b'pieces': [{b'tracker':[]} for _ in range(self.num_pieces)]
            }
            
            self.metadata_path = os.path.join(self.pieces_folder, f"{self.file_id}_metadata.bencode")
            self._save_metadata()
            
        
        os.makedirs(self.pieces_folder, exist_ok=True)
        
        self.check_status()

    def _verify_ip(self, src_ip):
        
        if src_ip is None or src_ip == "":
            raise ValueError("src_ip must be provided and cannot be empty.")
        

        try:
            ipaddress.ip_address(src_ip) 
        except ValueError:
            raise ValueError(f"Invalid src_ip format: {src_ip}. It must be a valid IP address.")
    
    def check_status(self):
        """Checks the currenlty available pieces and misssing pieces"""

        self._missing_pieces = set(range(self.num_pieces))

        if os.path.exists(self.file_dir):
            for filename in os.listdir(self.file_dir):
                # no integrity check only existance check
                if filename.startswith("piece_"):
                    piece_id = int(filename[6:])
                    self._missing_pieces.discard(piece_id)
                    self._available_pieces.add(piece_id)
                    self.file_metadata[b'pieces'][piece_id][b'tracker'].append(self.src_ip)
                    

    def split_pieces(self) -> None:
        """
        Split the file into pieces of predefined size and store them on disk.
        """
        if not self.file_path:
            raise ValueError("File path must be specified for splitting pieces.")
        
        self.check_status()
        if not self.missing_pieces:
            return 
        
        os.makedirs(self.file_dir, exist_ok=True)

        num_pieces = 0
        with open(self.file_path, 'rb') as file:
            while True:
                piece_data = file.read(self.piece_size)
                if not piece_data:
                    break
                piece_path = os.path.join(self.file_dir, f"piece_{num_pieces}")
                with open(piece_path, 'wb') as piece_file:
                    piece_file.write(piece_data)
                    self._available_pieces.add(num_pieces)
                num_pieces += 1

        self.file_metadata = {
            b'file_id': self.file_id,
            b'filename': self.filename,
            b'src_ip': self.src_ip,  # Placeholder for actual source IP
            b'num_pieces': num_pieces,
            b'pieces': [
                {
                    # b'checksum': '',  # not needed for this project as we don't expect any corruption
                    b'tracker': [self.src_ip]  # Source node already has this piece
                } for _ in range(num_pieces)
            ]
        }

        self._save_metadata()
        self.logger.debug(f"File split into {num_pieces} pieces")

    def _save_metadata(self) -> str:
        """
        Save metadata for the file including file ID, filename, source IP, and details of each piece.
        Metadata is encoded using bencode.
        """
    
        encoded_metadata = bencodepy.encode(self.file_metadata)
        with open(self.metadata_path, 'wb') as meta_file:
            meta_file.write(encoded_metadata)
        
        self.logger.debug(f"Metadata saved to {self.metadata_path}")

    @classmethod
    def from_metadata(cls, metadata_path: str, *args, **kwargs):
        """
        Create a FileManager instance for receiving nodes using metadata.
        """
        
        with open(metadata_path, 'rb') as meta_file:
            metadata = bencodepy.decode(meta_file.read())
            instance = cls(file_metadata=metadata, *args, **kwargs)
            instance.metadata_path = metadata_path
        os.makedirs(instance.file_dir, exist_ok=True)
        instance.logger.debug(f"FileManager instance created from metadata: {metadata_path}")
        return instance

    def receive_piece(self, piece_id: int, data: bytes, src_ip: str) -> None:
        """
        Write a file piece to disk at the designated folder.
        """
        if not self.file_metadata:
            raise ValueError("Metadata not set up. Run setup_for_pieces first.")
        
        self._verify_ip(src_ip)
        
        assert(piece_id in self.missing_pieces)



        # create piece folder it doesn't exit yet
        os.makedirs(self.file_dir, exist_ok=True)
        
        # should be in the format file_dir/file_id/piece_name
        piece_name = f"piece_{piece_id}"
        piece_path = os.path.join(self.file_dir, piece_name)
        self.logger.debug(f"Receiving piece {piece_id} from {src_ip}")
        with open(piece_path, 'wb') as piece_file:
            piece_file.write(data)
        
        self._available_pieces.add(piece_id)
        self._missing_pieces.discard(piece_id)
        
        # Update metadata tracker with the IP of the node that received the piece
       
        self.file_metadata[b'pieces'][piece_id][b'tracker'].append(src_ip)
        self._save_metadata();
        self.logger.debug(f"Piece {piece_id} received and saved to {piece_path}")
    
    def send_piece(self, piece_id: int) -> bytes:
        """
        return requested piece as a bytes object
        """
        
        assert(piece_id in self.available_pieces)
        
        piece_path = os.path.join(self.file_dir, f"piece_{piece_id}")
        self.logger.debug(f"Sending piece {piece_id}")
        with open(piece_path, 'rb') as piece_file:
            return piece_file.read()

    def reassemble(self) -> None:
        """
        Reassemble the complete file from pieces.
        """
        if not self.file_metadata:
            raise ValueError("Metadata not set up. Run setup_for_pieces first.")

        self.logger.debug(f"Reassembling file: {self.filename}")
        
        
        output_path = os.path.join(self.pieces_folder, f"reconstructed_{self.filename}")
        
        with open(output_path, 'wb') as output_file:
            for i in range(self.num_pieces):
                piece_path = os.path.join(self.file_dir, f"piece_{i}")
                assert(os.path.exists(piece_path))
                with open(piece_path, 'rb') as piece_file:
                    output_file.write(piece_file.read())
        
        self.logger.debug(f"File reassembled and saved to {output_path}")
    
    @property
    def missing_pieces(self) -> List[int]:
        """
        Return a list of pieces that are missing (i.e., have not been received by any node).
        """
        return self._missing_pieces

    @property
    def available_pieces(self) -> List[int]:
        """
        Return a list of pieces that are available (i.e., have been received by at least one node).
        """
        return self._available_pieces
