# HIXL Communication Performance Data

This document summarizes HIXL communication performance data across different Ascend platforms, organized by platform chapters.

---

## HIXL Measured Performance Data on Ascend A3 Chip in Selected Scenarios

### How to Read the Tables

- Total data volume: 128MiB.
- **Block** column: Each data block size (16K = 16 KiB, 1M = 1 MiB).
- Each transport protocol is shown in a separate table in order **FabricMem** → **ROCE** → **HCCS**; columns follow direction order: `D2rD`, `rD2D`, `D2rH`, `rH2D`, `H2rH`, `rH2H`, `H2rD`, `rD2H`.
- **HCCS** / **ROCE** are further split by path: **hixl_cs** first, then **communication domain** (default).
- **FabricMem** is further split into **AICPU unfold** and **Host unfold** tables; **Host unfold** uses **4 concurrent streams**.
- Values are effective bandwidth (GB/s); **Not supported** means the transport does not support that direction on this platform; **——** means data has not been collected yet.

### Single-Machine Data

#### FabricMem (AICPU unfold)

| **Block** | **D2rD** | **rD2D** | **D2rH** | **rH2D** | **H2rH** | **rH2H** | **H2rD** | **rD2H** |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 16K | 16.113 | 15.231 | 8.624 | 9.202 | 6.025 | 6.032 | 8.623 | 7.852 |
| 32K | 37.012 | 35.162 | 17.282 | 19.209 | 12.677 | 12.684 | 18.231 | 15.876 |
| 64K | 70.338 | 60.888 | 26.157 | 32.281 | 20.737 | 20.761 | 31.573 | 24.015 |
| 128K | 125.744 | 90.275 | 35.309 | 46.753 | 24.842 | 24.874 | 40.567 | 32.979 |
| 256K | 162.611 | 119.969 | 42.725 | 63.635 | 28.176 | 28.230 | 48.114 | 40.584 |
| 512K | 170.724 | 140.783 | 47.545 | 76.339 | 30.371 | 30.396 | 53.516 | 45.927 |
| 1M | 170.514 | 156.698 | 50.428 | 86.249 | 31.587 | 31.621 | 56.906 | 49.135 |
| 2M | 165.513 | 165.753 | 51.818 | 91.617 | 32.381 | 32.478 | 58.755 | 50.817 |

#### FabricMem (Host unfold, 4 concurrent streams)

| **Block** | **D2rD** | **rD2D** | **D2rH** | **rH2D** | **H2rH** | **rH2H** | **H2rD** | **rD2H** |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 16K | 2.874 | 2.820 | 2.913 | 2.935 | 1.952 | 1.346 | 2.070 | 2.116 |
| 32K | 5.714 | 5.677 | 5.950 | 6.004 | 3.926 | 2.654 | 4.182 | 4.304 |
| 64K | 11.427 | 11.213 | 11.561 | 11.823 | 7.833 | 5.012 | 8.327 | 8.711 |
| 128K | 22.602 | 22.799 | 23.191 | 23.491 | 15.783 | 9.145 | 16.656 | 17.525 |
| 256K | 45.023 | 44.911 | 45.585 | 46.418 | 31.242 | 18.649 | 32.971 | 34.958 |
| 512K | 89.443 | 91.489 | 53.287 | 90.972 | 33.844 | 33.676 | 60.472 | 53.059 |
| 1M | 175.102 | 165.744 | 54.020 | 101.405 | 34.083 | 34.038 | 61.700 | 53.530 |
| 2M | 181.431 | 182.613 | 54.185 | 101.702 | 34.203 | 34.208 | 61.955 | 53.999 |

#### ROCE (hixl_cs)

| **Block** | **D2rD** | **rD2D** | **D2rH** | **rH2D** | **H2rH** | **rH2H** | **H2rD** | **rD2H** |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 16K | 18.216 | 18.144 | 18.721 | 19.055 | 18.131 | 18.576 | 18.867 | 19.085 |
| 32K | 22.999 | 23.127 | 23.057 | 23.082 | 23.097 | 22.963 | 23.021 | 23.032 |
| 64K | 23.540 | 23.698 | 23.666 | 23.683 | 23.692 | 23.662 | 23.607 | 23.639 |
| 128K | 23.607 | 23.760 | 23.704 | 23.766 | 23.780 | 23.744 | 23.706 | 23.734 |
| 256K | 23.753 | 23.803 | 23.771 | 23.799 | 23.817 | 23.801 | 23.737 | 23.791 |
| 512K | 23.785 | 23.830 | 23.790 | 23.809 | 23.837 | 23.806 | 23.750 | 23.805 |
| 1M | 23.771 | 23.817 | 23.809 | 23.807 | 23.794 | 23.784 | 23.760 | 23.792 |
| 2M | 23.854 | 23.917 | 23.905 | 23.904 | 23.905 | 23.883 | 23.868 | 23.886 |

#### ROCE (communication domain)

| **Block** | **D2rD** | **rD2D** | **D2rH** | **rH2D** | **H2rH** | **rH2H** | **H2rD** | **rD2H** |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 16K | 12.501 | 10.445 | 12.802 | 11.971 | 10.914 | 11.015 | 12.838 | 11.615 |
| 32K | 6.946 | 6.713 | 6.875 | 6.759 | 6.926 | 6.831 | 6.871 | 6.761 |
| 64K | 10.846 | 10.689 | 10.843 | 10.684 | 10.805 | 10.69 | 10.837 | 10.705 |
| 128K | 17.579 | 17.41 | 17.564 | 17.387 | 17.552 | 17.406 | 17.558 | 17.381 |
| 256K | 23.02 | 22.853 | 23.026 | 22.856 | 22.98 | 22.853 | 22.996 | 22.885 |
| 512K | 23.457 | 23.385 | 23.452 | 23.354 | 23.419 | 23.378 | 23.426 | 23.394 |
| 1M | 23.518 | 23.424 | 23.513 | 23.378 | 23.442 | 23.423 | 23.431 | 23.45 |
| 2M | 23.641 | 23.572 | 23.636 | 23.524 | 23.574 | 23.545 | 23.574 | 23.573 |

#### HCCS (hixl_cs)

| **Block** | **D2rD** | **rD2D** | **D2rH** | **rH2D** | **H2rH** | **rH2H** | **H2rD** | **rD2H** |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 16K | 18.018 | 17.208 | Not supported | Not supported | Not supported | Not supported | 9.984 | 9.376 |
| 32K | 36.710 | 34.829 | Not supported | Not supported | Not supported | Not supported | 18.236 | 16.512 |
| 64K | 70.133 | 60.441 | Not supported | Not supported | Not supported | Not supported | 31.669 | 26.223 |
| 128K | 122.422 | 88.537 | Not supported | Not supported | Not supported | Not supported | 40.774 | 35.149 |
| 256K | 156.034 | 116.639 | Not supported | Not supported | Not supported | Not supported | 48.461 | 42.922 |
| 512K | 161.101 | 136.330 | Not supported | Not supported | Not supported | Not supported | 53.868 | 48.367 |
| 1M | 161.185 | 150.467 | Not supported | Not supported | Not supported | Not supported | 57.183 | 51.890 |
| 2M | 164.610 | 165.265 | Not supported | Not supported | Not supported | Not supported | 59.791 | 54.410 |

#### HCCS (communication domain)

| **Block** | **D2rD** | **rD2D** | **D2rH** | **rH2D** | **H2rH** | **rH2H** | **H2rD** | **rD2H** |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 16K | 15.983 | 15.016 | Not supported | Not supported | Not supported | Not supported | 7.180 | 7.217 |
| 32K | 31.97 | 30.106 | Not supported | Not supported | Not supported | Not supported | 13.487 | 13.343 |
| 64K | 60.293 | 52.557 | Not supported | Not supported | Not supported | Not supported | 23.636 | 22.226 |
| 128K | 108.384 | 80.165 | Not supported | Not supported | Not supported | Not supported | 29.658 | 28.408 |
| 256K | 146.208 | 110.322 | Not supported | Not supported | Not supported | Not supported | 34.267 | 33.201 |
| 512K | 158.308 | 132.348 | Not supported | Not supported | Not supported | Not supported | 37.354 | 36.511 |
| 1M | 157.44 | 143.571 | Not supported | Not supported | Not supported | Not supported | 39.000 | 38.343 |
| 2M | 158.812 | 158.189 | Not supported | Not supported | Not supported | Not supported | 40.484 | 39.856 |

### Dual-Machine Data

#### FabricMem (AICPU unfold)

| **Block** | **D2rD** | **rD2D** | **D2rH** | **rH2D** | **H2rH** | **rH2H** | **H2rD** | **rD2H** |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 16K | 4.282 | 4.468 | 4.430 | 4.556 | 3.658 | 3.681 | 3.538 | 3.640 |
| 32K | 9.831 | 10.247 | 9.632 | 10.017 | 7.992 | 8.014 | 7.855 | 7.649 |
| 64K | 18.419 | 19.005 | 16.863 | 18.014 | 14.415 | 14.471 | 14.657 | 14.123 |
| 128K | 32.156 | 33.104 | 26.952 | 30.014 | 16.058 | 16.304 | 16.225 | 16.337 |
| 256K | 51.698 | 54.238 | 38.259 | 47.160 | 17.308 | 17.822 | 17.237 | 17.717 |
| 512K | 72.708 | 77.331 | 48.507 | 62.549 | 18.368 | 18.576 | 17.865 | 18.167 |
| 1M | 91.918 | 102.410 | 55.817 | 78.565 | 18.866 | 19.050 | 18.163 | 18.782 |
| 2M | 105.036 | 117.977 | 59.895 | 88.828 | 19.221 | 19.291 | 18.499 | 19.074 |

#### FabricMem (Host unfold, 4 concurrent streams)

| **Block** | **D2rD** | **rD2D** | **D2rH** | **rH2D** | **H2rH** | **rH2H** | **H2rD** | **rD2H** |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 16K | 3.044 | 2.719 | 2.871 | 2.824 | 2.073 | 1.453 | 2.093 | 1.771 |
| 32K | 5.976 | 5.418 | 5.718 | 5.652 | 4.249 | 3.212 | 4.221 | 3.302 |
| 64K | 11.656 | 10.746 | 11.271 | 11.289 | 8.265 | 6.315 | 8.372 | 6.871 |
| 128K | 23.386 | 19.760 | 22.579 | 21.076 | 16.359 | 13.092 | 16.459 | 14.007 |
| 256K | 45.416 | 42.108 | 44.166 | 43.564 | 19.530 | 19.153 | 18.822 | 17.171 |
| 512K | 87.852 | 84.384 | 61.449 | 88.329 | 19.661 | 19.398 | 19.003 | 17.277 |
| 1M | 120.245 | 149.028 | 62.579 | 103.353 | 19.610 | 19.441 | 19.069 | 17.289 |
| 2M | 125.167 | 157.762 | 66.053 | 106.525 | 19.679 | 19.503 | 19.080 | 17.336 |

#### ROCE (hixl_cs)

| **Block** | **D2rD** | **rD2D** | **D2rH** | **rH2D** | **H2rH** | **rH2H** | **H2rD** | **rD2H** |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 16K | 18.367 | 17.814 | 18.606 | 18.701 | 18.396 | 18.618 | 18.835 | 18.903 |
| 32K | 23.151 | 23.040 | 23.092 | 23.122 | 22.963 | 22.867 | 23.122 | 23.118 |
| 64K | 23.713 | 23.548 | 23.645 | 23.652 | 23.490 | 23.518 | 23.677 | 23.622 |
| 128K | 23.778 | 23.673 | 23.710 | 23.678 | 23.553 | 23.639 | 23.703 | 23.682 |
| 256K | 23.803 | 23.674 | 23.741 | 23.682 | 23.605 | 23.674 | 23.776 | 23.659 |
| 512K | 23.820 | 23.681 | 23.771 | 23.783 | 23.584 | 23.658 | 23.780 | 23.662 |
| 1M | 23.788 | 23.663 | 23.725 | 23.728 | 23.607 | 23.549 | 23.770 | 23.687 |
| 2M | 23.933 | 23.828 | 23.888 | 23.903 | 23.772 | 23.770 | 23.914 | 23.784 |

#### ROCE (communication domain)

| **Block** | **D2rD** | **rD2D** | **D2rH** | **rH2D** | **H2rH** | **rH2H** | **H2rD** | **rD2H** |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 16K | 7.765 | 10.713 | 6.515 | 9.434 | 9.946 | 11.249 | 9.817 | 11.951 |
| 32K | 6.289 | 6.518 | 6.052 | 6.44 | 6.866 | 6.684 | 6.947 | 6.51 |
| 64K | 10.799 | 10.587 | 10.216 | 10.669 | 10.814 | 10.681 | 10.857 | 10.678 |
| 128K | 17.863 | 16.275 | 16.381 | 16.321 | 16.406 | 15.934 | 17.122 | 16.487 |
| 256K | 22.232 | 22.861 | 21.76 | 22.83 | 22.973 | 22.854 | 23.052 | 22.871 |
| 512K | 22.752 | 23.377 | 22.283 | 23.344 | 23.42 | 23.341 | 23.475 | 23.386 |
| 1M | 22.895 | 23.4 | 22.439 | 23.364 | 23.322 | 23.35 | 23.506 | 23.388 |
| 2M | 23.045 | 23.509 | 22.617 | 23.504 | 23.481 | 23.491 | 23.604 | 23.51 |

#### HCCS (communication domain)

| **Block** | **D2rD** | **rD2D** | **D2rH** | **rH2D** | **H2rH** | **rH2H** | **H2rD** | **rD2H** |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 16K | 3.471 | 5.041 | Not supported | Not supported | Not supported | Not supported | 4.002 | 4.044 |
| 32K | 6.911 | 10.09 | Not supported | Not supported | Not supported | Not supported | 7.724 | 7.692 |
| 64K | 12.967 | 18.747 | Not supported | Not supported | Not supported | Not supported | 14.429 | 13.471 |
| 128K | 22.87 | 32.633 | Not supported | Not supported | Not supported | Not supported | 16.257 | 15.859 |
| 256K | 37.465 | 53.585 | Not supported | Not supported | Not supported | Not supported | 17.464 | 17.587 |
| 512K | 54.564 | 77.99 | Not supported | Not supported | Not supported | Not supported | 18.186 | 18.576 |
| 1M | 71.708 | 98.584 | Not supported | Not supported | Not supported | Not supported | 18.519 | 18.969 |
| 2M | 89.985 | 117.531 | Not supported | Not supported | Not supported | Not supported | 18.907 | 19.287 |


---

## HIXL Measured Performance Data on Ascend A2 Chip in Selected Scenarios

### How to Read the Tables

- Total data volume: 128MiB.
- **Block** column: Each data block size (16K = 16 KiB, 1M = 1 MiB).
- Each transport protocol is shown in a separate table in order **ROCE** → **HCCS**; columns follow direction order: `D2rD`, `rD2D`, `D2rH`, `rH2D`, `H2rH`, `rH2H`, `H2rD`, `rD2H`.
- **ROCE** / **HCCS** are split by path: **hixl_cs** first, then **communication domain**.
- Values are effective bandwidth (GB/s); **Not supported** means the transport does not support that direction on this platform; **——** means data has not been collected yet.

### Single-Machine Data

#### ROCE (hixl_cs)

| **Block** | **D2rD** | **rD2D** | **D2rH** | **rH2D** | **H2rH** | **rH2H** | **H2rD** | **rD2H** |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 16K | 18.306 | 18.397 | 13.067 | 16.242 | 17.496 | 17.236 | 17.755 | 17.185 |
| 32K | 22.746 | 22.904 | 19.151 | 21.906 | 20.084 | 20.038 | 22.818 | 19.917 |
| 64K | 23.404 | 23.519 | 18.889 | 22.448 | 20.567 | 20.517 | 23.467 | 20.341 |
| 128K | 23.520 | 23.655 | 18.233 | 22.162 | 20.683 | 20.660 | 23.603 | 20.425 |
| 256K | 23.565 | 23.730 | 18.272 | 22.149 | 20.714 | 20.713 | 23.709 | 20.467 |
| 512K | 23.597 | 23.746 | 18.299 | 21.407 | 20.752 | 20.745 | 23.732 | 20.504 |
| 1M | 23.596 | 23.740 | 18.257 | 18.675 | 20.749 | 20.740 | 23.718 | 20.519 |
| 2M | 23.731 | 23.898 | 18.375 | 19.065 | 20.821 | 20.838 | 23.873 | 20.582 |

#### ROCE (communication domain)

| **Block** | **D2rD** | **rD2D** | **D2rH** | **rH2D** | **H2rH** | **rH2H** | **H2rD** | **rD2H** |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 16K | 0.716 | 0.701 | 0.698 | 0.698 | 0.713 | 0.699 | 0.722 | 0.678 |
| 32K | 1.42 | 1.397 | 1.43 | 1.387 | 1.421 | 1.384 | 1.409 | 1.398 |
| 64K | 2.84 | 2.778 | 2.887 | 2.562 | 2.842 | 2.793 | 2.881 | 2.776 |
| 128K | 5.735 | 5.538 | 5.714 | 5.395 | 5.868 | 5.658 | 5.601 | 5.66 |
| 256K | 10.909 | 11.273 | 10.634 | 10.923 | 10.634 | 11.254 | 11.304 | 10.442 |
| 512K | 15.566 | 20.366 | 17.8 | 16.299 | 15.47 | 19.64 | 14.788 | 16.763 |
| 1M | 18.789 | 23.676 | 21.069 | 20.391 | 17.469 | 19.286 | 21.858 | 18.351 |
| 2M | 19.778 | 24.042 | 21.072 | 21.99 | 15.244 | 20.492 | 21.695 | 20.506 |

#### HCCS (hixl_cs)

| **Block** | **D2rD** | **rD2D** | **D2rH** | **rH2D** | **H2rH** | **rH2H** | **H2rD** | **rD2H** |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 16K | 8.903 | 10.265 | Not supported | Not supported | Not supported | Not supported | Not supported | Not supported |
| 32K | 12.778 | 15.453 | Not supported | Not supported | Not supported | Not supported | Not supported | Not supported |
| 64K | 15.780 | 19.748 | Not supported | Not supported | Not supported | Not supported | Not supported | Not supported |
| 128K | 17.787 | 22.810 | Not supported | Not supported | Not supported | Not supported | Not supported | Not supported |
| 256K | 19.018 | 24.752 | Not supported | Not supported | Not supported | Not supported | Not supported | Not supported |
| 512K | 19.680 | 25.752 | Not supported | Not supported | Not supported | Not supported | Not supported | Not supported |
| 1M | 20.031 | 26.281 | Not supported | Not supported | Not supported | Not supported | Not supported | Not supported |
| 2M | 20.284 | 26.814 | Not supported | Not supported | Not supported | Not supported | Not supported | Not supported |

#### HCCS (communication domain)

| **Block** | **D2rD** | **rD2D** | **D2rH** | **rH2D** | **H2rH** | **rH2H** | **H2rD** | **rD2H** |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 16K | 1.47 | 1.387 | Not supported | Not supported | Not supported | Not supported | Not supported | Not supported |
| 32K | 2.493 | 2.744 | Not supported | Not supported | Not supported | Not supported | Not supported | Not supported |
| 64K | 5.439 | 5.497 | Not supported | Not supported | Not supported | Not supported | Not supported | Not supported |
| 128K | 6.568 | 10.991 | Not supported | Not supported | Not supported | Not supported | Not supported | Not supported |
| 256K | 12.062 | 22.037 | Not supported | Not supported | Not supported | Not supported | Not supported | Not supported |
| 512K | 19.571 | 26.405 | Not supported | Not supported | Not supported | Not supported | Not supported | Not supported |
| 1M | 19.853 | 27.027 | Not supported | Not supported | Not supported | Not supported | Not supported | Not supported |
| 2M | 20.515 | 27.345 | Not supported | Not supported | Not supported | Not supported | Not supported | Not supported |

### Dual-Machine Data

#### ROCE (hixl_cs)

| **Block** | **D2rD** | **rD2D** | **D2rH** | **rH2D** | **H2rH** | **rH2H** | **H2rD** | **rD2H** |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 16K | 18.290 | 18.056 | 16.988 | 17.823 | 16.432 | 16.170 | 17.161 | 17.161 |
| 32K | 22.742 | 22.599 | 19.790 | 21.705 | 19.607 | 19.909 | 21.680 | 19.841 |
| 64K | 23.469 | 23.332 | 20.334 | 22.326 | 20.086 | 20.289 | 22.433 | 20.336 |
| 128K | 23.565 | 23.488 | 20.470 | 22.487 | 20.238 | 19.838 | 22.555 | 20.519 |
| 256K | 23.661 | 23.564 | 20.415 | 22.510 | 19.852 | 19.745 | 22.618 | 20.557 |
| 512K | 23.682 | 23.548 | 20.499 | 22.548 | 17.321 | 19.826 | 22.607 | 20.538 |
| 1M | 23.652 | 23.525 | 20.504 | 22.521 | 17.345 | 18.071 | 22.605 | 20.582 |
| 2M | 23.827 | 23.664 | 20.593 | 22.679 | 20.410 | 18.194 | 22.722 | 20.639 |

#### ROCE (communication domain)

| **Block** | **D2rD** | **rD2D** | **D2rH** | **rH2D** | **H2rH** | **rH2H** | **H2rD** | **rD2H** |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 16K | 0.718 | 0.703 | 0.715 | 0.685 | 0.717 | 0.699 | 0.721 | 0.676 |
| 32K | 1.404 | 1.296 | 1.413 | 1.324 | 1.429 | 1.285 | 1.432 | 1.309 |
| 64K | 2.847 | 2.705 | 2.853 | 2.784 | 2.777 | 2.802 | 2.789 | 2.771 |
| 128K | 5.502 | 5.611 | 5.65 | 5.663 | 5.508 | 5.543 | 5.786 | 5.579 |
| 256K | 11.203 | 11.055 | 11.259 | 10.76 | 11.044 | 11.09 | 11.292 | 10.829 |
| 512K | 18.532 | 16.295 | 15.368 | 17.117 | 17.814 | 17.982 | 18.429 | 17.921 |
| 1M | 21.223 | 24.006 | 18.981 | 20.713 | 19.838 | 19.961 | 20.509 | 14.599 |
| 2M | 20.729 | 24.134 | 18.283 | 22.878 | 19.543 | 19.394 | 20.276 | 18.739 |
