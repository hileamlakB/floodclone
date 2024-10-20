import unittest
import os
import shutil
import bencodepy
import hashlib
import math
import random
from floodclone.filesystem import FileManager  

class TestFileManager(unittest.TestCase):

    def setUp(self):
        # Setup a temporary directory for testing
        self.test_dir = "test_pieces"
        self.test_dir2 = "recv_test_pieces"
        os.makedirs(self.test_dir, exist_ok=True)
        self.test_file_path = os.path.join(self.test_dir, "test_file.txt")


        # Create a test file
        with open(self.test_file_path, 'w') as f:
            f.write("This is a test file for FileManager. " * 100)  

        self.manager = FileManager(file_path=self.test_file_path, piece_size=64, src_ip='127.0.0.1', pieces_folder=self.test_dir)
    

    def tearDown(self):
        # Clean up the test directory after tests
        try:
            shutil.rmtree(self.test_dir)
            shutil.rmtree(self.test_dir2)
        except Exception:
            pass

    def test_split_pieces(self):
        
        self.manager.split_pieces()
        
        # Calculate the expected number of pieces based on file size and piece size
        file_size = os.path.getsize(self.test_file_path)
        expected_num_pieces = math.ceil(file_size / self.manager.piece_size)
        
        # Check if the metadata was created correctly
        self.assertEqual(self.manager.file_metadata[b'num_pieces'], expected_num_pieces)
        self.assertEqual(len(self.manager.available_pieces), expected_num_pieces)

        # Check if the correct number of pieces is created on disk
        file_id = self.manager.file_metadata[b'file_id']
        file_dir = os.path.join(self.test_dir, f"dir_{file_id}")

        self.assertTrue(os.path.exists(file_dir))
        for i in range(expected_num_pieces):
            piece_path = os.path.join(file_dir, f"piece_{i}")
            self.assertTrue(os.path.exists(piece_path))
    
    def test_split_pieces2(self):
        """
        make sure nothing happens if someone tries to split a file for a second time
        """
        
        self.manager.split_pieces()
        self.manager.split_pieces()
        
        
    def test_reassemble(self):
        # Test if file is correctly reassembled from pieces
        self.manager.split_pieces()
        self.manager.reassemble()

        # Check if the reassembled file matches the original file content
        reassembled_file_path = os.path.join(self.test_dir,  f"reconstructed_{os.path.basename(self.test_file_path)}")
        with open(self.test_file_path, 'r') as original_file:
            original_content = original_file.read()
        with open(reassembled_file_path, 'r') as reassembled_file:
            reassembled_content = reassembled_file.read()
        self.assertEqual(original_content, reassembled_content)

    def test_receive_piece(self):
        # Ensure the manager is initialized with metadata
        piece_id = 0
        expected_data = b'This is a test file for FileManager. This is a test file for Fil'
        
        # Call the method under test
        self.manager.receive_piece(piece_id, expected_data, '127.0.0.1')
        
        # Read back the data from the file to verify
        piece_path = os.path.join(self.manager.file_dir, f"piece_{piece_id}")
        with open(piece_path, 'rb') as piece_file:
            saved_data = piece_file.read()
        
        # Assert that the saved data matches the expected data
        self.assertEqual(saved_data, expected_data)

    def test_missing_and_available_pieces(self):
        
        self.manager.split_pieces()
        
    
        self.test_manager = FileManager.from_metadata(self.manager.metadata_path, pieces_folder=self.test_dir2, src_ip='127.0.0.2')

        # Simulate receiving some random pieces
        num_pieces = math.ceil(os.path.getsize(self.test_file_path) / self.manager.piece_size)
        received_pieces = set(random.sample(range(num_pieces), k=int(num_pieces * 0.7)))  # Receive 70% of the pieces

        for i in received_pieces:
            self.test_manager.receive_piece(i, b'This is a test file for FileManager. This is a test file for Fil', '127.0.0.1')

        # Check missing pieces
        missing = self.test_manager.missing_pieces
        self.assertEqual(missing, set(range(num_pieces)) - set(received_pieces))  # Some pieces should be missing

        # Check available pieces
        available = self.test_manager.available_pieces
        self.assertEqual(available, set(received_pieces))  # Only received pieces should be available

    def test_send_receive_and_reassemble(self):
    

        self.manager.split_pieces()
        # Create a new FileManager instance from metadata
        receiver_manager = FileManager.from_metadata(self.manager.metadata_path, pieces_folder=self.test_dir2, src_ip='127.0.0.3')
       
        # Simulate sending and receiving all pieces
        for piece_id in range(self.manager.file_metadata[b'num_pieces']):
            sent_data = self.manager.send_piece(piece_id)
            receiver_manager.receive_piece(piece_id, sent_data, '127.0.0.1')

        # Reassemble the file on the receiving node
        receiver_manager.reassemble()

        # Compare the original and reassembled files using a hash function
        original_hash = self._calculate_file_hash(self.test_file_path)
        reassembled_file_path = os.path.join(self.test_dir2, f"reconstructed_{os.path.basename(self.test_file_path)}")
        reassembled_hash = self._calculate_file_hash(reassembled_file_path)

        self.assertEqual(original_hash, reassembled_hash)

    def _calculate_file_hash(self, file_path):
        """Helper method to calculate the SHA-256 hash of a file."""
        hash_sha256 = hashlib.sha256()
        with open(file_path, 'rb') as f:
            for chunk in iter(lambda: f.read(4096), b""):
                hash_sha256.update(chunk)
        return hash_sha256.hexdigest()

if __name__ == '__main__':
    unittest.main()
