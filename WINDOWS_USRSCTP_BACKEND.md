# SCTP Windows usrsctp Backend Implementation Plan

## Current Status (as of this session)
Windows uses UDP fallback for SCTP transport. Full `usrsctp` backend integration requires:
1. Network I/O bridge (userland socket → usrsctp)
2. Send callback integration
3. RX thread coordination

## Implementation Architecture

### Phase 1: Core I/O Backend (Not Yet Implemented)
**Files to modify**: `src/common/sctp_socket.cpp`

```cpp
// Userland network I/O proxy for usrsctp on Windows
class SctpSocketWindowsBackend {
    // Userland socket for sending raw datagrams
    UdpSocket udpProxy_;
    
    // Receive thread for proxying packets into usrsctp
    std::thread ioThread_;
    
    // Callback: inject received UDP datagrams into SCTP stack
    static void *receive_callback(void *addr, void *data, size_t len, 
                                  uint8_t ecn_bits, uint16_t flags);
    
    // Main I/O loop: recv from UDP → concinput() to SCTP stack
    void ioLoopWindows();
};
```

### Phase 2: Send Path Integration
When usrsctp sends data via registered address:
1. usrsctp library calls registered send callback
2. Callback marshals data + address info
3. Data forwarded into UdpSocket for transmission
4. Application RX loop receives response datagrams
5. Datagrams fed back via usrsctp_conninput()

### Phase 3: Buffer Management
- Circular buffers for RX/TX queuing
- Careful sequencing to avoid callback deadlocks
- Thread synchronization via mutexes

## Key API Functions
```cpp
int usrsctp_register_address(unsigned int AF_FAMILY, void *address);
int usrsctp_deregister_address(void *old_address);

void usrsctp_conninput(void *addr, const void *data, size_t datalen, 
                       uint8_t ecn_bits);
```

## Challenges & Trade-offs

**Challenge**: Callback ↔ I/O Thread Coordination
- **Risk**: Deadlock if main sends while callback holding lock
- **Mitigation**: Lockless queue or interrupt-safe callbacks

**Challenge**: Address Translation
- Our transport API uses `remoteIp_`, `remotePort_`
- usrsctp expects struct sockaddr
- Need strict mapping between representations

**Challenge**: SCTP-over-UDP Encapsulation
- Current: framing magic (0x4E47/0x3842) handles protocol demux
- Concern: usrsctp may add its own SCTP-over-UDP framing
- Solution: Use native SCTP port + verify interop

## Benefits When Complete
1. **Full multi-homing**: Windows can use all sctp_connectx() features
2. **Notifications**: Real SCTP_ASSOC_CHANGE events on Windows
3. **Performance tuning**: All SCTP socket options functional
4. **Linux parity**: Windows feature set matches Linux kernel SCTP

## Testing Strategy
1. Mock usrsctp callbacks in test harness
2. Verify send/receive loop closure
3. Validate buffer overflow handling
4. Compare wire format with Linux baseline (tcpdump)

## Deferral Note
Full backend implementation deferred due to:
- Complexity of callback synchronization
- Required usrsctp deep integration
- Risk of regression vs. stable UDP fallback
- Time investment vs. current UDP fallback adequacy

**Current recommendation**: Keep UDP fallback for now; implement full backend
if multi-homing + notifications become critical for production Windows deployment.

See also: [SCTP Enhancement Session Notes](/memories/session/sctp_enhancements_plan.md)
