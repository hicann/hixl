# Security Declaration

## User Account Recommendations

To ensure security and minimize permissions, you are not advised to use administrator accounts such as `root`.

## File Permission Control

- You are advised to set the `umask` value of the running system to `0027` or higher on the host (including the host) and container, to ensure that the default highest permission of new folders is `750` and that of new files is `640`.
- You are advised to take security measures such as permission control on sensitive files, including personal privacy data, business assets, and source files. Refer to "Appendix A-Recommended maximum permissions for files and folders" to learn about the permissions for installation directories and public data files in this project.
- Permission control is required during installation and use. You are advised to set permissions by referring to "Appendix A-Recommended maximum permissions for files and folders."

## Build Security Statement

- When you are compiling and installing this project from the source code, some intermediate files will be generated. After the compilation is complete, you are advised to perform permission control on the intermediate files to ensure file security.

## Execution Security Statement

- If an exception occurs during running, the process exits and error information is printed. You are advised to locate the error cause based on the error information.

## Public Network Address Statement

The public network addresses contained in the code of this project are as follows.

|      Type     |                                           Open-Source Code Address                                          |                            File                            |             Public IP Address/Public URL/Domain Name/Email Address/Compressed File Address            |                   Description                   |
| :------------: |:------------------------------------------------------------------------------------------:|:----------------------------------------------------------| :---------------------------------------------------------- |:-----------------------------------------|
|  Dependency | N/A | cmake/fetch_cann_cmake.cmake | https://gitcode.com/cann/cmake.git | Fetch the cann-cmake public build scripts from GitCode as the compilation dependency.|
---

## Vulnerability Handling Mechanism

[Vulnerability Management](https://gitcode.com/cann/community/blob/master/security/security.md)

## Appendix

### A-Recommended maximum permissions for files and folders

| Type          | Maximum Linux Permission|
| -------------- | ---------------  |
| Home directory                       |   750 (rwxr-x---)           |
| Program files (including scripts and library files)      |   550 (r-xr-x---)            |
| Program file directory                     |   550 (r-xr-x---)           |
| Configuration files                         |  640 (rw-r-----)            |
| Configuration file directory                     |   750 (rwxr-x---)           |
| Log files (recorded or archived)       |  440 (r--r-----)            |
| Log files (being recorded)               |    640 (rw-r-----)          |
| Log file directory                     |   750 (rwxr-x---)           |
| Debug files                        |  640 (rw-r-----)        |
| Debug file directory                    |   750 (rwxr-x---) |
| Temporary file directory                     |   750 (rwxr-x---)  |
| Maintenance and upgrade file directory                 |   770 (rwxrwx---)   |
| Service data files                     |   640 (rw-r-----)   |
| Service data file directory                 |   750 (rwxr-x---)     |
| Key components, private keys, certificates, and ciphertext file directory   |  700 (rwx------)     |
| Key components, private keys, certificates, and ciphertext files       | 600 (rw-------)     |
| APIs and scripts for encryption and decryption           |   500 (r-x------)       |
