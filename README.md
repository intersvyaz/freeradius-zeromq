# FreeRADIUS 3.x ZeroMQ module

Simple module for performing ZeroMQ PUSH operations.

### Config examples
```
zeromq zeromq_push_example {
    # Server address string
    # Read more about string format here: [http://api.zeromq.org/4-1:zmq-connect]
    server = "tcp://127.0.0.1:5670/"
    
    # Connection pool
    pool {
        start = ${thread[pool].start_servers}
        min = ${thread[pool].min_spare_servers}
        max = ${thread[pool].max_servers}
        spare = ${thread[pool].max_spare_servers}
        uses = 0
        lifetime = 0
        idle_timeout = 60
    }

    # Data format ("raw" or "bson" is supported)
    format = bson

    # Data to send
    data = "{ \
        \"ip\": \"%{Framed-Ip-Address}\", \
        \"nas_ip\": \"%{Nas-Ip-Address}\", \
        \"login\": \"%{User-Name}\" \
    }"
}
```
