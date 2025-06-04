# fjdispatchlite

## Introduction

When an instance needs to perform asynchronous operations or receive messages from external sources asynchronously, it's common to delegate tasks to a worker thread.  
However, tightly coupling a class or instance with a specific thread is generally considered poor design. The real goal is to delegate tasks per operation, not to manage threads manually.

To achieve this kind of task delegation, the *dispatch* design pattern is commonly used.

One of the most well-known and widely used implementations of this pattern is Grand Central Dispatch (GCD), which is part of Apple’s CoreFoundation.  
GCD enables concurrent code execution on multicore hardware efficiently.

*Note: This is different from the "dispatch" used in GUI frameworks that route messages across layers — don't confuse the two.*

## Implementation

**fjdispatchlite** is a prototype implementation of the *dispatch* pattern using C++11.  
It's intended for educational purposes and to help understand the concept.  
For production use, it is strongly recommended to use mature libraries such as [XDispatch](http://opensource.mlba-team.de/xdispatch/docs/current/index.html) or similar.

## Build

```sh
% cmake .
% make
