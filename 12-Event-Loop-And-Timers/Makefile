all:
	g++ -Wall -Wextra -O2 -std=c++14 server.cpp hashtable.cpp avl.cpp zset.cpp connection_io.cpp do_cmd.cpp new_connect.cpp request.cpp -o server
	g++ -Wall -Wextra -O2 -std=c++14 client.cpp -o client

clean:
	rm server client