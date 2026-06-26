# Build from Source Code

## Environment Setup
This project supports build from source code. Before compiling the source code, ensure that the CANN Toolkit has been installed. Before running all samples, ensure that the driver and firmware have been installed. Before running Python samples, ensure that the OPS package has been installed.

Select an installation method based on the following description.

| Installation Mode| Description|Scenario|
| :--- | :--- | :--- |
| Deployment using Docker| Docker images provide an efficient CANN deployment method. Currently, it applies only to Atlas A2 products with Ubuntu OS.|This method is suitable for developers who have Ascend devices and need to quickly set up the environment.|
| Manual installation| - |This method is suitable for developers who have Ascend devices and want to manually install the CANN package or experience the latest master branch capabilities.|

### Scenario I: Deployment Using Docker

**1. Install the firmware and driver.** See [CANN Software Installation](https://hiascend.com/document/redirect/CannCommercialInstSoftware).

**2. Install the dependencies, CANN Toolkit, and CANN ops package.**
 - **x86 build image address**: `swr.cn-north-4.myhuaweicloud.com/ci_cann/ubuntu24.04_x86:lv6_v1.1039`
 - **Arm build image address**: `swr.cn-north-4.myhuaweicloud.com/ci_cann/ubuntu24.04_arm:lv6_v1.1039`

  Recommended usage:

  ```bash
  image=${Select a matching image based on the local machine architecture}

  # 1. Pull the matching build image
  docker pull ${image}
  # 2. Create and access the container
  # Assume that your NPU devices are installed in /dev/davinci0 and /dev/davinci1, and the NPU driver is installed in /usr/local/Ascend.
  docker run \
    --name env_for_hixl_build \
    --device /dev/davinci0 \
    --device /dev/davinci1 \
    --device /dev/davinci_manager \
    --device /dev/devmm_svm \
    --device /dev/hisi_hdc \
    --cap-add SYS_PTRACE \
    -v /usr/local/dcmi:/usr/local/dcmi \
    -v /usr/local/bin/npu-smi:/usr/local/bin/npu-smi \
    -v /usr/bin/hccn_tool:/usr/bin/hccn_tool \
    -v /usr/local/Ascend/driver/lib64/:/usr/local/Ascend/driver/lib64/ \
    -v /usr/local/Ascend/driver/tools/:/usr/local/Ascend/driver/tools/ \
    -v /usr/local/Ascend/driver/version.info:/usr/local/Ascend/driver/version.info \
    -v /etc/ascend_install.info:/etc/ascend_install.info \
    -it ${image} bash
  ```

  > [!NOTE]NOTE
  > - `--cap-add SYS_PTRACE`: Adds the `SYS_PTRACE` permission when creating a Docker to support memory leak detection during [local verification (Tests)](#local-verification-tests).
  > - For more Docker options, run the `docker --help` command.

  The installation path of the CANN package for the build image is `/home/jenkins/Ascend`. If you need to use a different CANN version outside the image, manually install the CANN package in Docker by referring to the following sections.

  For more image versions and how to use them, see [Ascend-CANN Images](https://www.hiascend.com/developer/ascendhub/detail/17da20d1c2b6493cb38765adeba85884).


### Scenario II: Manual Installation

**Scenario 1: Experience the master branch or perform development based on the master branch.**

If you want to experience the latest capabilities of the master branch, download the software package from [the download link](https://ascend.devcloud.huaweicloud.com/artifactory/cann-run-mirror/software/master) and perform the following steps. For detailed instructions, see [CANN Software Installation](https://hiascend.com/document/redirect/CannCommercialInstSoftware).

1. Install the firmware and driver. See [CANN Software Installation](https://hiascend.com/document/redirect/CannCommercialInstSoftware).

2. Install the community edition CANN Toolkit.

    ```bash
    # Ensure that the installation package is executable
    chmod +x Ascend-cann-toolkit_${cann_version}_linux-${arch}.run
    # Installation command
    ./Ascend-cann-toolkit_${cann_version}_linux-${arch}.run --install --install-path=${install_path}
    ```

3. Install the community edition CANN ops package.

    ```bash
    # Ensure that the installation package is executable
    chmod +x Ascend-cann-${soc_name}-ops_${cann_version}_linux-${arch}.run
    # Installation command
    ./Ascend-cann-${soc_name}-ops_${cann_version}_linux-${arch}.run --install --install-path=${install_path}
    ```
    - `\$\{cann\_version\}`: CANN package version.
    - `\$\{arch\}`: CPU architecture, such as `aarch64` and `x86_64`.
    - `\$\{soc\_name\}`: NPU model name.
    - `\$\{install\_path\}`: installation path. The CANN package and Toolkit package must be in the same path. For the `root` user, the default path is `/usr/local/Ascend`.

**Scenario 2: Experience a released version or perform development based on a released version.**

To experience the capabilities of the **officially released CANN package**, visit [the CANN download center](https://www.hiascend.com/cann/download) and select the CANN package (CANN 8.5.0 or later).


## Environment Verification

After installing the CANN packages, verify that the environment and driver are normal.

-   **Check NPUs.**
    ```bash
    # Run the npu-smi command. If the device information is displayed properly, the driver is normal.
    npu-smi info
    ```
-   **Check CANN installation.**
    ```bash
    # Check the version field of CANN Toolkit (default installation path). <arch> indicates the CPU architecture (AArch64 or x86_64).
    cat /usr/local/Ascend/cann/<arch>-linux/ascend_toolkit_install.info
    # Check the version field of CANN ops package (default installation path). <arch> indicates the CPU architecture (AArch64 or x86_64).
    cat /usr/local/Ascend/cann/<arch>-linux/ascend_ops_install.info
    ```

## Environment Variable Configuration

Run the appropriate command to make the environment variables take effect.
```bash
# Default installation path (using the root user as an example; for a non-root user, replace /usr/local with ${HOME})
source /usr/local/Ascend/cann/set_env.sh
# Custom installation path
# source ${install_path}/cann/set_env.sh
```
## Source Code Compilation

### Install Third-Party Open-Source Dependencies

The following lists the dependencies required for source code compilation. Check the version requirements.

  ```shell
  # Run the following commands for Ubuntu/Debian. For other OSs, Use proper commands for other OS.
  sudo apt-get install cmake bash ccache
  ```
- GCC 7.3.x - 14.2.x
- Python 3.9.x - 3.14.x
- CMake >= 3.16.0
- Bash >= 5.1.16 (Address sanitization is enabled in the test cases. Low versions of bash may trigger false memory leak detections in `system` calls.)
- unzip (extracts the ZIP packages of third-party open-source software)
- ccache (optional, a compiler cache optimization tool used to speed up incremental builds)

During compilation, HIXL depends on the following third-party open-source software.

| Open-Source Software| Version| Download Address|
|---|---|---|
| googletest | 1.14.0 | [googletest-1.14.0.tar.gz](https://gitcode.com/cann-src-third-party/googletest/releases/download/v1.14.0/googletest-1.14.0.tar.gz) |
| json | 3.11.3 | [include.zip](https://gitcode.com/cann-src-third-party/json/releases/download/v3.11.3/include.zip) |
| makeself | 2.5.0 | [makeself-release-2.5.0-patch1.tar.gz](https://gitcode.com/cann-src-third-party/makeself/releases/download/release-2.5.0-patch1.0/makeself-release-2.5.0-patch1.tar.gz) |
| pybind11 | 2.13.6 | [pybind11-2.13.6.tar.gz](https://gitcode.com/cann-src-third-party/pybind11/releases/download/v2.13.6/pybind11-2.13.6.tar.gz) |
| cann-cmake | master-006 | [cmake-master-006.tar.gz](https://cann-3rd.obs.cn-north-4.myhuaweicloud.com/cmake/cmake-master-006.tar.gz) |

> [!NOTE]NOTE
> If you download the packages from other addresses, ensure that the version numbers match exactly.

### Download Source Code

Run the following commands to download the source code of this repository:
```bash
git clone https://gitcode.com/cann/hixl.git
```

> [!NOTE]NOTE
> When using HTTPS on GitCode, configure and use a personal access token instead of the login password for operations such as cloning and pushing.

If your build environment cannot access the Internet, you cannot download code via `git`. In this case, you need to download the source code in an environment with Internet access and manually upload it to the target environment.
- In an environment with Internet access, go to [the project homepage](https://gitcode.com/cann/hixl), and download the source code via `ZIP download` or `clone`.
- Connect to the offline environment and upload the source code to the specified directory. If you download a compressed package, decompress it.


### Compile Source Code

If your build environment can access the Internet, the compilation process will automatically download the required third-party open-source software listed above. Run the following command to perform the compilation:

```bash
# Default installation path (root user: /usr/local/Ascend; non-root user: ${HOME}/Ascend)
bash build.sh
```

If your build environment cannot access the Internet, download the required open-source packages in an environment with Internet access and manually upload them to your build environment.

Create a `{your_3rd_party_path}` directory in the build environment to store the third-party open-source software.

```bash
mkdir -p {your_3rd_party_path}
```

After uploading the third-party open-source software packages to `{your_3rd_party_path}`, run the following command to perform the build:
```bash
bash build.sh --cann_3rd_lib_path={your_3rd_party_path}
```

After the build is successful, the `cann-hixl_${cann_version}_linux-${arch}.run` file is generated in the `build_out` directory.
- `${cann_version}`: CANN version.
- `${arch}`: CPU architecture, such as `aarch64` and `x86_64`.
- If sample verification or benchmark verification is required, add the `--examples` parameter during the build.
- For more execution options, run `-h` or refer to the following table.
  ```
  bash build.sh -h
  ```

| Parameter| Description| Default Value|
|---|---|---|
| `-h, --help` | Displays help information.| - |
| `-v, --verbose` | Displays detailed compilation commands.| - |
| `-j<N>` | Sets the number of threads used for build.| 8 |
| `--build_type=<Release\|Debug>`<br>`--build-type=<Release\|Debug>` | Sets the build type.| Release |
| `--cann_3rd_lib_path=<PATH>`<br>`--cann-3rd-lib-path=<PATH>` | Sets the installation path of the third-party dependency packages.| `./third_party` |
| `--output_path=<PATH>`<br>`--output-path=<PATH>` | Sets the build output path.| `./build_out` |
| `--pkg` | Builds a `.run` file (reserved).| - |
| `--examples` | Builds the sample and benchmark test.| OFF |
| `--asan` | Enables address sanitization for memory leak detection.| OFF |
| `--cov` | Enables code coverage.| OFF |
| `--sign-script=<PATH>`<br>`--sign_script=<PATH>` | Sets the path for the signature script.| - |
| `--enable-sign` | Enables the signature function.| - |

## Local Verification (Tests)

Use the test cases in the `tests` directory for local verification.

- Install Dependencies
    ```bash
    # Install the dependencies in the requirements.txt file under the root directory
    pip3 install -r requirements.txt
    ```
  To view the test coverage locally, install `coverage` and add the Python 3 binary path to the `PATH` environment variable. 

     ```shell
     pip3 install coverage
     # Replace `PYTHON3_HOME` with the actual Python installation directory
     export PATH=$PATH:$PYTHON3_HOME/bin
     ```

- Execute Test Cases

    ```bash
    # Default installation path (root user: /usr/local/Ascend/; non-root user: ${HOME}/Ascend)
    bash tests/run_test.sh
    # If you have uploaded the third-party open-source software package to {your_3rd_party_path}, run the following command:
    bash tests/run_test.sh --cann_3rd_lib_path={your_3rd_party_path}
    ```

- For more execution options, run `-h`.
  ```
  bash tests/run_test.sh -h
  ```

## Installation

Install the `.run` file generated during [Compile Source Code](#source-code-compilation).
- Note that the installation path (default or specified) must be the same as the path where the Toolkit package is installed.
```bash
# To specify an installation path, add --install-path=${cann_install_path}
./cann-hixl_${cann_version}_linux-${arch}.run --full --quiet --pylocal
```
- --`full`: full installation mode
- --`quiet`: silent installation, skipping human-machine interaction.
- --`pylocal`: whether to install the `.whl` package to the HIXL installation path.
    - If selected, the `.whl` package is installed under `${cann_install_path}/cann/python/site-packages`.
    - If not selected, `.whl` is installed in the local python path, for example, `/usr/local/python3.7.5/lib/python3.7/site-packages`.
- For more installation options, run `--help`.

### Supplementary Notes on Signature
- The compiled `cann-hixl_<version>_linux-<arch>.run` package contains `cann-hixl-compat.tar.gz` (HIXL-compatible upgrade package).
- The `cann-hixl-compat.tar.gz` package is loaded to the device during service startup and is verified by the driver for security purpose.
- Source-built `cann-hixl-compat.tar.gz` does not contain a signature header. Therefore, you must disable the driver's signature verification mechanism.
- Disable signature verification as follows:
  - Disabling signature verification depends on the Ascend NPU driver software package (Ascend HDK 25.5.T2.B001 or later). Use the `npu-smi` tool matching Ascend HDK to query the version and disable signature verification. For details, see [Query Basic Information](https://support.huawei.com/enterprise/en/doc/EDOC1100540362/4a8adb57?idPath=23710424|251366513|254884019|261408772|252764743), [Set Custom Signature Verification Capability Status](https://support.huawei.com/enterprise/en/doc/EDOC1100540362/3152813c?idPath=23710424|251366513|254884019|261408772|252764743), and [Set Signature Verification Mode](https://support.huawei.com/enterprise/en/doc/EDOC1100540362/a484ba7b?idPath=23710424|251366513|254884019|261408772|252764743). Run the commands on a physical machine as the `root` user.
  - The following commands use device 0 as an example (where `-i` specifies the device ID):
    ```
    npu-smi info # Query basic information, including the driver version
    npu-smi set -t custom-op-secverify-enable -i 0 -d 1 # Enable custom signature verification
    npu-smi set -t custom-op-secverify-mode -i 0 -d 0 # Disable signature verification
    ```
**After the installation is complete, run the sample test by referring to [Sample Running](../../examples/README_en.md) or run the benchmark test by referring to [Benchmarks](../../benchmarks/README.md).**
