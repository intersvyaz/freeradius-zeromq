# rlm_zeromq

### Build instructions
Separate build Using CMAKE:
```bash
mkdir build
cd build
cmake ..
make
```

For in-tree build just copy source files to `src/modules/rlm_zeromq` and add module in Make.inc MODULES section.

### Config examples
```
# example
zeromq example {
    server = "tcp://127.0.0.1:5670/"
    format = "raw"
    lazy_connect = yes
    "data" = "{ \
        \"ip\": \"%{Framed-Ip-Address}\", \
        \"nas_ip\": \"%{Nas-Ip-Address}\", \
        \"login\": \"%{User-Name}\" \
    }" 
}
```
