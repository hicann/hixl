# HIXL Communication Performance Data

This document summarizes HIXL communication performance data across different Ascend platforms, organized by platform chapters.

---

## HIXL Measured Performance Data on Ascend A2 Chip in Selected Scenarios

### How to Read the Tables

- Total data volume: 128MiB.
- **Block** column: Each data block size (16K = 16 KiB, 1M = 1 MiB).
- **Direction+Transport** column: For example, `D2rD HCCS` means writing from local Device to remote Device (HCCS transport), `rH2D ROCE` means reading from remote Host to local Device (ROCE transport).

### Single-Machine Data

| **Block** | **D2rD<br>HCCS** | **D2rD<br>ROCE** | **D2rH<br>HCCS** | **D2rH<br>ROCE** | **H2rH<br>HCCS** | **H2rH<br>ROCE** | **H2rD<br>HCCS** | **H2rD<br>ROCE** | **rD2D<br>HCCS** | **rD2D<br>ROCE** | **rH2D<br>HCCS** | **rH2D<br>ROCE** | **rH2H<br>HCCS** | **rH2H<br>ROCE** | **rD2H<br>HCCS** | **rD2H<br>ROCE** |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 16K | 1.47 | 0.716 | Not supported | 0.698 | Not supported | 0.713 | Not supported | 0.722 | 1.387 | 0.701 | Not supported | 0.698 | Not supported | 0.699 | Not supported | 0.678 |
| 32K | 2.493 | 1.42 | Not supported | 1.43 | Not supported | 1.421 | Not supported | 1.409 | 2.744 | 1.397 | Not supported | 1.387 | Not supported | 1.384 | Not supported | 1.398 |
| 64K | 5.439 | 2.84 | Not supported | 2.887 | Not supported | 2.842 | Not supported | 2.881 | 5.497 | 2.778 | Not supported | 2.562 | Not supported | 2.793 | Not supported | 2.776 |
| 128K | 6.568 | 5.735 | Not supported | 5.714 | Not supported | 5.868 | Not supported | 5.601 | 10.991 | 5.538 | Not supported | 5.395 | Not supported | 5.658 | Not supported | 5.66 |
| 256K | 12.062 | 10.909 | Not supported | 10.634 | Not supported | 10.634 | Not supported | 11.304 | 22.037 | 11.273 | Not supported | 10.923 | Not supported | 11.254 | Not supported | 10.442 |
| 512K | 19.571 | 15.566 | Not supported | 17.8 | Not supported | 15.47 | Not supported | 14.788 | 26.405 | 20.366 | Not supported | 16.299 | Not supported | 19.64 | Not supported | 16.763 |
| 1M | 19.853 | 18.789 | Not supported | 21.069 | Not supported | 17.469 | Not supported | 21.858 | 27.027 | 23.676 | Not supported | 20.391 | Not supported | 19.286 | Not supported | 18.351 |
| 2M | 20.515 | 19.778 | Not supported | 21.072 | Not supported | 15.244 | Not supported | 21.695 | 27.345 | 24.042 | Not supported | 21.99 | Not supported | 20.492 | Not supported | 20.506 |

### Dual-Machine Data

| **Block** | **D2rD<br>ROCE** | **D2rH<br>ROCE** | **H2rH<br>ROCE** | **H2rD<br>ROCE** | **rD2D<br>ROCE** | **rH2D<br>ROCE** | **rH2H<br>ROCE** | **rD2H<br>ROCE** |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 16K | 0.718 | 0.715 | 0.717 | 0.721 | 0.703 | 0.685 | 0.699 | 0.676 |
| 32K | 1.404 | 1.413 | 1.429 | 1.432 | 1.296 | 1.324 | 1.285 | 1.309 |
| 64K | 2.847 | 2.853 | 2.777 | 2.789 | 2.705 | 2.784 | 2.802 | 2.771 |
| 128K | 5.502 | 5.65 | 5.508 | 5.786 | 5.611 | 5.663 | 5.543 | 5.579 |
| 256K | 11.203 | 11.259 | 11.044 | 11.292 | 11.055 | 10.76 | 11.09 | 10.829 |
| 512K | 18.532 | 15.368 | 17.814 | 18.429 | 16.295 | 17.117 | 17.982 | 17.921 |
| 1M | 21.223 | 18.981 | 19.838 | 20.509 | 24.006 | 20.713 | 19.961 | 14.599 |
| 2M | 20.729 | 18.283 | 19.543 | 20.276 | 24.134 | 22.878 | 19.394 | 18.739 |

---

## HIXL Measured Performance Data on Ascend A3 Chip in Selected Scenarios

### Single-Machine Data

| **Block** | **D2rD<br>HCCS** | **D2rD<br>ROCE** | **D2rD<br>FabricMem** | **D2rH<br>HCCS** | **D2rH<br>ROCE** | **D2rH<br>FabricMem** | **H2rH<br>HCCS** | **H2rH<br>ROCE** | **H2rH<br>FabricMem** | **H2rD<br>HCCS** | **H2rD<br>ROCE** | **H2rD<br>FabricMem** | **rD2D<br>HCCS** | **rD2D<br>ROCE** | **rD2D<br>FabricMem** | **rH2D<br>HCCS** | **rH2D<br>ROCE** | **rH2D<br>FabricMem** | **rH2H<br>HCCS** | **rH2H<br>ROCE** | **rH2H<br>FabricMem** | **rD2H<br>HCCS** | **rD2H<br>ROCE** | **rD2H<br>FabricMem** |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 16K | 15.983 | 12.501 | 2.874 | Not supported | 12.802 | 2.913 | Not supported | 10.914 | 1.952 | 7.180 | 12.838 | 2.070 | 15.016 | 10.445 | 2.820 | Not supported | 11.971 | 2.935 | Not supported | 11.015 | 1.346 | 7.217 | 11.615 | 2.116 |
| 32K | 31.97 | 6.946 | 5.714 | Not supported | 6.875 | 5.950 | Not supported | 6.926 | 3.926 | 13.487 | 6.871 | 4.182 | 30.106 | 6.713 | 5.677 | Not supported | 6.759 | 6.004 | Not supported | 6.831 | 2.654 | 13.343 | 6.761 | 4.304 |
| 64K | 60.293 | 10.846 | 11.427 | Not supported | 10.843 | 11.561 | Not supported | 10.805 | 7.833 | 23.636 | 10.837 | 8.327 | 52.557 | 10.689 | 11.213 | Not supported | 10.684 | 11.823 | Not supported | 10.69 | 5.012 | 22.226 | 10.705 | 8.711 |
| 128K | 108.384 | 17.579 | 22.602 | Not supported | 17.564 | 23.191 | Not supported | 17.552 | 15.783 | 29.658 | 17.558 | 16.656 | 80.165 | 17.41 | 22.799 | Not supported | 17.387 | 23.491 | Not supported | 17.406 | 9.145 | 28.408 | 17.381 | 17.525 |
| 256K | 146.208 | 23.02 | 45.023 | Not supported | 23.026 | 45.585 | Not supported | 22.98 | 31.242 | 34.267 | 22.996 | 32.971 | 110.322 | 22.853 | 44.911 | Not supported | 22.856 | 46.418 | Not supported | 22.853 | 18.649 | 33.201 | 22.885 | 34.958 |
| 512K | 158.308 | 23.457 | 89.443 | Not supported | 23.452 | 53.287 | Not supported | 23.419 | 33.844 | 37.354 | 23.426 | 60.472 | 132.348 | 23.385 | 91.489 | Not supported | 23.354 | 90.972 | Not supported | 23.378 | 33.676 | 36.511 | 23.394 | 53.059 |
| 1M | 157.44 | 23.518 | 175.102 | Not supported | 23.513 | 54.020 | Not supported | 23.442 | 34.083 | 39.000 | 23.431 | 61.700 | 143.571 | 23.424 | 165.744 | Not supported | 23.378 | 101.405 | Not supported | 23.423 | 34.038 | 38.343 | 23.45 | 53.530 |
| 2M | 158.812 | 23.641 | 181.431 | Not supported | 23.636 | 54.185 | Not supported | 23.574 | 34.203 | 40.484 | 23.574 | 61.955 | 158.189 | 23.572 | 182.613 | Not supported | 23.524 | 101.702 | Not supported | 23.545 | 34.208 | 39.856 | 23.573 | 53.999 |

### Dual-Machine Data

| **Block** | **D2rD<br>HCCS** | **D2rD<br>ROCE** | **D2rD<br>FabricMem** | **D2rH<br>HCCS** | **D2rH<br>ROCE** | **D2rH<br>FabricMem** | **H2rH<br>HCCS** | **H2rH<br>ROCE** | **H2rH<br>FabricMem** | **H2rD<br>HCCS** | **H2rD<br>ROCE** | **H2rD<br>FabricMem** | **rD2D<br>HCCS** | **rD2D<br>ROCE** | **rD2D<br>FabricMem** | **rH2D<br>HCCS** | **rH2D<br>ROCE** | **rH2D<br>FabricMem** | **rH2H<br>HCCS** | **rH2H<br>ROCE** | **rH2H<br>FabricMem** | **rD2H<br>HCCS** | **rD2H<br>ROCE** | **rD2H<br>FabricMem** |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 16K | 3.471 | 7.765 | 3.862 | Not supported | 6.515 | 4.927 | Not supported | 9.946 | 3.337 | 4.002 | 9.817 | 4.680 | 5.041 | 10.713 | 3.920 | Not supported | 9.434 | 4.514 | Not supported | 11.249 | 3.570 | 4.044 | 11.951 | 3.716 |
| 32K | 6.911 | 6.289 | 7.553 | Not supported | 6.052 | 10.170 | Not supported | 6.866 | 6.889 | 7.724 | 6.947 | 9.862 | 10.09 | 6.518 | 7.493 | Not supported | 6.44 | 9.273 | Not supported | 6.684 | 6.824 | 7.692 | 6.51 | 7.560 |
| 64K | 12.967 | 10.799 | 15.559 | Not supported | 10.216 | 20.364 | Not supported | 10.814 | 14.165 | 14.429 | 10.857 | 17.175 | 18.747 | 10.587 | 15.500 | Not supported | 10.669 | 18.496 | Not supported | 10.681 | 14.126 | 13.471 | 10.678 | 14.817 |
| 128K | 22.87 | 17.863 | 31.675 | Not supported | 16.381 | 40.147 | Not supported | 16.406 | 18.747 | 16.257 | 17.122 | 18.470 | 32.633 | 16.275 | 31.115 | Not supported | 16.321 | 36.348 | Not supported | 15.934 | 18.896 | 15.859 | 16.487 | 18.381 |
| 256K | 37.465 | 22.232 | 61.331 | Not supported | 21.76 | 56.907 | Not supported | 22.973 | 19.143 | 17.464 | 23.052 | 18.709 | 53.585 | 22.861 | 62.984 | Not supported | 22.83 | 71.603 | Not supported | 22.854 | 19.284 | 17.587 | 22.871 | 18.650 |
| 512K | 54.564 | 22.752 | 104.465 | Not supported | 22.283 | 61.494 | Not supported | 23.42 | 19.433 | 18.186 | 23.475 | 18.927 | 77.99 | 23.377 | 117.661 | Not supported | 23.344 | 93.823 | Not supported | 23.341 | 19.398 | 18.576 | 23.386 | 18.749 |
| 1M | 71.708 | 22.895 | 115.874 | Not supported | 22.439 | 63.908 | Not supported | 23.322 | 19.511 | 18.519 | 23.506 | 18.967 | 98.584 | 23.4 | 138.030 | Not supported | 23.364 | 102.319 | Not supported | 23.35 | 19.462 | 18.969 | 23.388 | 18.830 |
| 2M | 89.985 | 23.045 | 125.888 | Not supported | 22.617 | 65.790 | Not supported | 23.481 | 19.601 | 18.907 | 23.604 | 18.993 | 117.531 | 23.509 | 150.366 | Not supported | 23.504 | 106.651 | Not supported | 23.491 | 19.587 | 19.287 | 23.51 | 18.873 |
