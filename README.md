---
page_type: sample
languages:
  - c
  - asm
name: 'Azure RTOS Microsoft Learning Samples'
description: 'Sample projects for Azure RTOS Microsoft Learning courses how.'
products:
  - azure-rtos
---

# Azure RTOS Microsoft Learning Samples

This repo contains sample projects for

- [Azure RTOS ThreadX Learning Path](https://learn.microsoft.com/training/paths/azure-rtos-threadx/)
- [Azure RTOS NetX Duo Learning path](https://learn.microsoft.com/training/paths/azure-rtos-netx-duo/)

## Get started

### Use GitHub Codespaces

[GitHub Codespaces](https://github.com/features/codespaces) is the preferred way to building and run these sample if you have your GitHub account enabled for this feature. Otherwise, you can still use it with the [local dev container](https://code.visualstudio.com/docs/remote/containers) or set up the toolchain by your own.

Follow the [Set up environment](https://learn.microsoft.com/training/modules/introduction-azure-rtos/2-set-up-environment) unit to get started with the samples.

#### Directory layout

    .
    ├── cmake                        # CMakelist files for building the project
    ├── docs                         # Documentation supplements
    ├── courses                      # Source code for learning paths
    │   ├── netxduo                  # NetX Duo samples
    │   └── threadx                  # ThreadX samples
    ├── libs                         # Submoduled ThreadX and NetX Duo source code
    └── tools                        # Required scripts for using NetX Duo within the container

### Use Visual Studio

You can also find the sample projects that can be built and run with Visual Studio in the [release page](https://github.com/Azure-Samples/azure-rtos-learn-samples/releases/tag/vs). An alternative for using the sample projects. Follow the [get started](#get-started) section above or the readme file in the `.zip` to learn how to use it.

## Resources

- [Azure RTOS](https://aka.ms/rtos)
- [Azure RTOS on GitHub](https://github.com/azure-rtos)
- [PDF: Real-Time Embedded Multithreading Using ThreadX 4th Edition](https://github.com/Azure-Samples/azure-rtos-learn-samples/releases/download/book/Real-Time_Embedded_Multithreading_with_ThreadX_4th_Edition.pdf)

For some common issues we found, please visit [Wiki](https://github.com/Azure-Samples/azure-rtos-learn-samples/wiki).
