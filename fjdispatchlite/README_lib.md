# FJ Utility Modules Overview

This repository provides a set of lightweight, low-level utility modules designed for
embedded Linux and performance-sensitive systems.
The focus is on **predictable behavior**, **low overhead**, and **explicit control over memory and concurrency**.

Each module is independent but designed to work well together.

---

## fjtypes

`fjtypes` defines common fundamental types, macros, and small utilities shared across all other modules.

### Purpose
- Provide a **single, consistent type system** across the project
- Reduce platform-dependent ambiguity (size, signedness, alignment)
- Centralize frequently used definitions

### Typical contents
- Fixed-width integer aliases
- Boolean and result/status types
- Utility macros (alignment, min/max, container_of, etc.)
- Compile-time helpers

### Design notes
- Header-only
- No dynamic allocation
- No dependency on other modules

---

## fjdispatchlite

`fjdispatchlite` is a lightweight task dispatching framework similar in spirit to Grand Central Dispatch (GCD),
but designed for **embedded Linux and constrained environments**.

### Purpose
- Execute asynchronous tasks on worker threads
- Decouple task producers from execution context
- Provide predictable and debuggable scheduling behavior

### Key features
- Fixed or bounded worker threads
- Explicit task queues
- Low-overhead synchronization
- No hidden thread creation

### Typical use cases
- Event-driven processing
- Media or I/O pipelines
- Background task execution without blocking callers

### Design notes
- Uses pthreads
- Avoids dynamic thread explosion
- Designed for observability and stability in long-running systems

---

## fjfixvector

`fjfixvector` is a fixed-capacity, contiguous container similar to `std::vector`,
but without dynamic memory allocation after initialization.

### Purpose
- Provide vector-like semantics with **deterministic memory usage**
- Avoid heap fragmentation and allocation jitter

### Key features
- Fixed maximum capacity
- Contiguous storage
- O(1) indexed access
- Explicit control over lifetime

### Typical use cases
- Real-time or near-real-time systems
- Shared memory–compatible data structures
- Environments where `malloc` must be avoided

### Design notes
- Capacity defined at construction time
- No automatic reallocation
- Trivially inspectable memory layout

---

## fjfixmap

`fjfixmap` is a fixed-capacity associative container (map/dictionary) with predictable memory usage.

### Purpose
- Provide key–value storage without dynamic allocation
- Offer deterministic behavior under load

### Key features
- Fixed maximum number of entries
- Simple lookup semantics
- No iterator invalidation due to reallocation

### Typical use cases
- Configuration tables
- Small routing or lookup tables
- Shared memory data structures

### Design notes
- Backed by fixed-size storage
- Trade-offs favor predictability over asymptotic optimality
- Suitable for embedded and safety-critical systems

---

## fjsharedmem

`fjsharedmem` provides utilities for managing shared memory regions between processes.

### Purpose
- Enable **inter-process communication (IPC)** via shared memory
- Provide a structured and reusable approach to shared memory management

### Key features
- Named shared memory regions
- Explicit initialization and attachment
- Support for synchronization primitives (mutex, condition variables)
- Clear ownership and lifecycle rules

### Typical use cases
- Multi-process architectures
- Media pipelines split across processes
- Producer–consumer models using shared buffers

### Design notes
- Built on top of `mmap` and POSIX shared memory concepts
- Emphasizes correctness and debuggability
- Designed to integrate with fixed-size containers such as `fjfixvector` and `fjfixmap`

---

## Design Philosophy

Across all modules, the following principles apply:

- **Predictability over convenience**
- **Explicit memory ownership**
- **Minimal hidden behavior**
- **Embedded-friendly design**

These modules are intended to be building blocks for systems where
standard C++ containers or heavyweight frameworks are unsuitable.
