# Quick Start

This guide introduces how to run a HIXL C++ HCCS sample, including starting the server/client processes and performing the READ transfer with data verification, helping users quickly verify the basic transfer capability of HIXL over the `hccs:device` link after completing the build.

> **Note**: `hixl_example_quickstart` is a quick-start sample intended only to verify that the feature works. On failure paths, the sample exits the process immediately and does not perform resource cleanup such as memory deregistration, disconnect, or engine finalize. For production use, refer to other samples and implement proper error handling and resource release.

## Prerequisites

- The CANN environment variables have been configured. In the default installation path, run:

  ```bash
  source /usr/local/Ascend/cann/set_env.sh
  ```

- The samples have been compiled:

  ```bash
  bash build.sh --examples
  ```

- Two interconnected devices have been selected. If the execution fails, refer to [Sample Execution](../../examples/README_en.md) to check device connectivity and TLS configuration.
- This sample only supports Atlas A2 training series products/Atlas A2 inference series products, and Atlas A3 training series products/Atlas A3 inference series products.

## Running the Sample

Go to the C++ sample executable directory:

```bash
cd build/examples/cpp
```

Run the following commands in two terminals. Start the server first, and then start the client.

```bash
# Terminal 1: server
./hixl_example_quickstart --role=server

# Terminal 2: client
./hixl_example_quickstart --role=client
```

When the following logs appear in the client terminal, the READ transfer and data verification are successful:

```text
[INFO] TransferSync READ completed
[INFO] Verify success
```

## Verifying Functions

The source code of this sample is [examples/cpp/hixl_example_quickstart.cpp](../../examples/cpp/hixl_example_quickstart.cpp), which demonstrates the core HIXL workflow with minimal code and verifies the basic transfer capability of HIXL over the `hccs:device` link. This sample is for functional verification only: the success path disconnects and releases resources, while failure paths call `exit` directly without cleanup.

- Start two independent processes: server and client.
- Each side initializes the Hixl engine and registers device memory.
- The server finishes local `RegisterMem` first, then exchanges the buffer address through a socket, so the address received by the client is already transferable.
- The client initiates a READ transfer to read data from the server.
- Verify the read data on the client to validate device-side data transfer and data consistency.
- On success, disconnect, deregister memory, and finalize the engine after the transfer; on failure, exit immediately without the above cleanup.

## Default Parameters

The preceding commands do not explicitly specify the device and engine addresses. The sample default values are used:

- The client uses `device 0` by default, and the local engine address is `127.0.0.1:16000`.
- The server uses `device 2` by default, and the local engine address is `127.0.0.1:16001`.

For more parameter descriptions and HIXL sample details, see [HIXL Samples](../../examples/cpp/README_en.md#2-hixl-samples).
