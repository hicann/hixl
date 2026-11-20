# Header Files and Library Files

<!-- md-trans-meta sourceCommit=673c23f74e276071e977827c16399fea3bc501e7 translatedAt=2026-10-27T02:13:51.476Z pushedAt=2026-10-27T02:46:32.138Z nl=2 -->

Install the firmware, driver, and CANN packages before referencing the hixl API header and library files when compiling and running applications.

The hixl API headers are in `${INSTALL_DIR}/include/`, and the libraries are in `${INSTALL_DIR}/lib64/`. Replace ${INSTALL_DIR} with the CANN installation path. For a root installation, the default path is **/usr/local/Ascend/cann**.

Include the required files based on the hixl APIs you use. The following table describes the header files.

**Table 1** Header Files

| API Header File | Purpose | Library File |
| --- | --- | --- |
| llm\_datadist/llm\_datadist.h | Provides APIs with KV Cache semantics for large-model KV Cache transfers, supporting role management, connection management, KV Cache registration, contiguous KV Cache transfers, and KV Block transfers. | libllm\_datadist.so |
| hixl/hixl.h | Provides easy-to-use one-sided zero-copy direct data transfers for point-to-point scenarios, supporting initialization/deinitialization, memory registration/deregistration, connection establishment/disconnection, and data transfer. | libcann\_hixl.so |
| cs/hixl\_cs.h | Provides one-sided zero-copy direct data transfers through a Client-Server integration model. Supports Client creation/destruction, connection establishment, memory registration/unregistration, remote memory descriptor retrieval, and transfers, as well as Server creation/destruction and memory registration/unregistration. | libcann\_hixl.so |
