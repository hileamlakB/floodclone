import hashlib
import logging
import math
import os
import shutil
import tempfile
import time
import matplotlib.pyplot as plt
import bencodepy
from typing import List
import sys
import os

# Add the root directory to sys.path
root_path = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.append(root_path)

from floodclone.filesystem import FileManager

# Function to create test files
def create_test_file(file_path: str, size_in_mb: int):
    with open(file_path, 'wb') as f:
        f.write(b'\0' * size_in_mb * 1024 * 1024)

# Measure direct copy time
def measure_direct_copy_time(src, dst):
    start = time.time()
    shutil.copy(src, dst)
    return time.time() - start

# Measure time using FileManager for splitting, transferring, and reassembling
def measure_piece_transfer_time(file_size_mb):
    sender_folder = tempfile.mkdtemp(prefix="sender_")
    receiver_folder = tempfile.mkdtemp(prefix="receiver_")
    original_file = os.path.join(sender_folder, f"test_file_{file_size_mb}MB.txt")
    create_test_file(original_file, file_size_mb)
    
    try:
        sender = FileManager(file_path=original_file, pieces_folder=sender_folder, src_ip='127.0.0.1')
        start = time.time()
        sender.split_pieces()
        
        receiver = FileManager.from_metadata(sender.metadata_path, receiver_folder, src_ip='127.0.0.2')
        for piece_id in range(sender.num_pieces):
            data = sender.send_piece(piece_id)
            receiver.receive_piece(piece_id, data, sender.src_ip)
        receiver.reassemble()
        return time.time() - start
    finally:
        shutil.rmtree(sender_folder)
        shutil.rmtree(receiver_folder)

# Test with different file sizes
file_sizes = [1, 10, 50, 100, 200, 400]
direct_copy_times = []
piece_transfer_times = []

for size in file_sizes:
    test_file = f"test_file_{size}MB.txt"
    create_test_file(test_file, size)
    
    # Measure direct copy time
    direct_copy_times.append(measure_direct_copy_time(test_file, f"copy_{test_file}"))
    os.remove(test_file)
    os.remove(f"copy_{test_file}")
    
    # Measure FileManager method time
    piece_transfer_times.append(measure_piece_transfer_time(size))

# Plot the results
plt.figure(figsize=(10, 6))
plt.plot(file_sizes, direct_copy_times, label='Direct Copy', marker='o')
plt.plot(file_sizes, piece_transfer_times, label='Piece Transfer & Reassemble', marker='o')
plt.xlabel('File Size (MB)')
plt.ylabel('Time (Seconds)')
plt.title('Comparison of File Transfer Methods')
plt.legend()
plt.grid(True)
plt.show()
