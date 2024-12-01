# gdb --args 
./floodclone --mode destination \
  --node-name d1 \
  --src-name src \
  --pieces-dir /tmp/d1/pieces \
  --network-info '{"d1": {"src": [["d1-eth0", 1, ["d1"]]]}, "src": {"d1": [["src-eth0", 1, ["src"]]]}}' \
  --ip-map '{"d1": [["d1-eth0", "10.0.0.1"]], "src": [["src-eth0", "10.0.0.2"]]}' \
  --timestamp-file /tmp/d1/pieces/completion_time
