# 739 AFS
## Compile and run
### First compile
```bash
cmake . # Should also handle grpc installing
make -j
```
### Subsequent compile and run
```bash
make -j
./server
./client -f ./mount_point
```

## To add a new operation
1. Find a operation to implement https://libfuse.github.io/doxygen/structfuse__operations.html
2. Insert stub function in `client.cpp`
    ```c
    static int do_getattr(const char* path, struct stat* st) {
        return GET_PDATA->client.c_getattr(path, st);
    }

    int main() {
        ...
        operations.getattr = do_getattr;
        ...
    }
    ```
3. Define new protobuf in `protos/hello.proto` if needed
    - Compile protobuf by running `./gen_proto.sh`
    ```protobuf
    message PathRequest { string path = 1; }
    message GetAttrResponse {
        int32 ret = 1;
        Stat stat = 2;
    }
    rpc s_getattr(PathRequest) returns (GetAttrResponse) {}
    ```

4. Implement client function in `client_grpc.cpp`
    ```cpp
    class GRPCClient {
        int c_getattr(const char *path, struct stat *st) {
            printf("[getattr] %s\n", path);
            ...
        }
    }
    ```
5. Implement server function in `server.cpp`
    ```cpp
    class gRPCServiceImpl final : public gRPCService::Service {
        Status s_getattr(ServerContext *context, const afs::PathRequest *req,
                        afs::GetAttrResponse *reply) override {
            ...
            return Status::OK;
        }
    };
    ```
