# NetworkDoc - Distributed Document Collaboration System

A fully-featured distributed document collaboration system built in C, similar to Google Docs, supporting multi-user editing with strict access control, persistence, and efficient search.

## System Architecture

The system consists of three core components:

1. **Name Server (NM)**: Central coordinator that maintains file metadata, manages access control, and maps filenames to storage servers
2. **Storage Server (SS)**: Nodes responsible for storing file data/metadata, supporting direct client requests for file operations
3. **Client**: User interface program allowing file operations over the network

All components communicate via TCP sockets using a custom message protocol.

## Features

### Core Functionality (150 points)
- ✅ **CREATE**: Create empty files with automatic load balancing across storage servers
- ✅ **READ**: Display full file contents with permission checking
- ✅ **WRITE**: Sentence-level locking with atomic word-level edits (WRITE...ETIRW)
- ✅ **DELETE**: Owner-only deletion with metadata cleanup
- ✅ **VIEW**: List files with multiple display modes (-a, -l, -al)
- ✅ **INFO**: Comprehensive file metadata (size, permissions, timestamps)
- ✅ **STREAM**: Word-by-word streaming with 0.1s delay
- ✅ **LIST**: Display all registered users
- ✅ **UNDO**: One-step undo for any user (per-file)
- ✅ **ADDACCESS/REMACCESS**: Granular permission management
- ✅ **EXEC**: Execute file contents as shell commands

### System Requirements (40 points)
- ✅ **Data Persistence**: SQLite-based storage surviving restarts
- ✅ **Access Control**: Rigorous permission enforcement (owner/read/write)
- ✅ **Logging**: Comprehensive logging with timestamps, IPs, operations
- ✅ **Error Handling**: Uniform error codes and clear messages
- ✅ **Efficient Search**: Trie-based file lookup (O(m) where m = filename length)

### Protocol Design (10 points)
- ✅ **Startup**: Name Server starts first on fixed port (8080)
- ✅ **Registration**: SS and clients register with NM on startup
- ✅ **Dynamic SS Addition**: New storage servers can join anytime
- ✅ **Atomic Operations**: Sentence-level locking with WRITE/ETIRW protocol

### Bonus Features (50 points)
- ✅ **Hierarchical Folders**: CREATEFOLDER command
- ✅ **Checkpoints**: Create, list, and revert to file snapshots
- ✅ **Access Requests**: Request/approve/reject access flow
- ✅ **Replication**: Automatic file replication across storage servers
- ✅ **Fault Tolerance**: Automatic failover to replica servers

## Building the System

### Prerequisites
```bash
# Install dependencies (Ubuntu/Debian)
sudo apt-get update
sudo apt-get install build-essential sqlite3 libsqlite3-dev libreadline-dev

# Install dependencies (Fedora/RHEL)
sudo dnf install gcc make sqlite sqlite-devel readline-devel
```

### Compilation
```bash
# Build all components
make

# Build specific component
make nameserver
make storageserver
make client

# Clean build artifacts
make clean
```

## Running the System

### 1. Start Name Server
```bash
./bin/nameserver
```
The Name Server listens on port 8080 by default.

### 2. Start Storage Servers
Start multiple storage servers on different ports for replication and load balancing:

```bash
# Terminal 2: Start first storage server
./bin/storageserver 9001

# Terminal 3: Start second storage server (for replication)
./bin/storageserver 9002

# Terminal 4: Start third storage server (optional)
./bin/storageserver 9003
```

### 3. Start Clients
Launch multiple clients with different usernames:

```bash
# Terminal 5: Start client for user 'alice'
./bin/client alice

# Terminal 6: Start client for user 'bob'
./bin/client bob
```

## Usage Examples

### Basic File Operations

```bash
# Create a new file
docs++> CREATE document.txt

# Write content (sentence-level editing)
docs++> WRITE document.txt 0
Lock acquired for sentence 0. Enter word edits:
Format: <word_index> <new_content>
Type 'ETIRW' when done.

Current sentence: 

> 0 Hello
Word 0 updated to 'Hello'
> 1 World!
Word 1 updated to 'World!'
> ETIRW
Write completed successfully

# Read file content
docs++> READ document.txt

=== Content of 'document.txt' ===
Hello World!

# View file info
docs++> INFO document.txt

=== Info for 'document.txt' ===
Words: 2 | Characters: 12 | Sentences: 1 | Modified: 2025-10-28 16:30:45
```

### Access Control

```bash
# Grant read access to another user
docs++> ADDACCESS -R document.txt bob
Access granted to bob

# Grant write access
docs++> ADDACCESS -W document.txt bob
Access granted to bob

# Revoke access
docs++> REMACCESS document.txt bob
Access revoked from bob

# Request access (from bob's client)
docs++> REQUESTACCESS document.txt R
Access request submitted
```

### File Listing

```bash
# List your files
docs++> VIEW

# List all files in system
docs++> VIEW -a

# List with details
docs++> VIEW -l
- document.txt    alice  2 words 1 sentences  2025-10-28 16:30:45

# List all files with details
docs++> VIEW -al
```

### Advanced Features

```bash
# Stream file word-by-word
docs++> STREAM document.txt
=== Streaming 'document.txt' ===
Hello World! 
[Stream complete]

# Create a checkpoint
docs++> CHECKPOINT document.txt version1
Checkpoint 'version1' created

# List checkpoints
docs++> LISTCHECKPOINTS document.txt
Checkpoints:
  version1 - 2025-10-28 16:35:00

# Revert to checkpoint
docs++> REVERT document.txt version1
Reverted to checkpoint 'version1'

# Undo last change
docs++> UNDO document.txt
Undo successful for 'document.txt'

# Execute file as shell script
docs++> EXEC script.sh

# Create folder
docs++> CREATEFOLDER documents

# List users
docs++> LIST
Registered Users:
  - alice
  - bob
```

## Architecture Details

### Message Protocol

All network communication uses a structured message format:

```c
typedef struct {
    char type[32];           // Message type (CREATE, READ, WRITE, etc.)
    char username[64];       // Username of requester
    char filename[256];      // Target filename
    char data[65536];        // Message payload
    int error_code;          // Error code (0 = success)
    char error_msg[256];     // Error message
} Message;
```

Messages are prefixed with a 4-byte length field (network byte order) for reliable transmission.

### Sentence-Level Locking

The WRITE protocol implements fine-grained locking:

1. Client requests lock for specific sentence from Name Server
2. Name Server checks if sentence is already locked
3. If available, lock is granted and SS info returned
4. Client reads current sentence, performs edits interactively
5. Client commits changes with ETIRW command
6. Name Server releases lock and updates modification timestamp

### Replication Strategy

- Files are automatically replicated to a secondary storage server
- Primary server is selected using least-loaded algorithm
- Replica server is selected as next available server
- Writes are mirrored to replica (eventually consistent)
- Reads fall back to replica if primary is unavailable

### Data Persistence

**Name Server Database** (`data/nameserver.db`):
- `files`: File metadata (owner, timestamps, counts, storage locations)
- `access_control`: User permissions per file
- `checkpoints`: Checkpoint metadata
- `undo_history`: Undo state per file
- `access_requests`: Pending access requests

**Storage Server Database** (`data/storage_<port>/metadata.db`):
- `file_metadata`: Local file statistics
- `checkpoints`: Checkpoint references

**File Storage**:
- `data/storage_<port>/<filename>`: Actual file content
- `data/storage_<port>/undo/<filename>`: Undo snapshots
- `data/storage_<port>/checkpoints/<filename>_<tag>`: Checkpoint snapshots

### Efficient Search

File lookup uses a **Trie (prefix tree)** data structure:
- Insert/Search/Delete: O(m) where m = filename length
- Much faster than O(n) linear search for large file counts
- Supports prefix-based search for future autocomplete features

### Error Handling

Comprehensive error codes:
- `ERR_SUCCESS (0)`: Operation successful
- `ERR_FILE_NOT_FOUND (1)`: File does not exist
- `ERR_FILE_EXISTS (2)`: File already exists
- `ERR_PERMISSION_DENIED (3)`: Insufficient permissions
- `ERR_LOCKED (4)`: Resource currently locked
- `ERR_INVALID_PARAM (5)`: Invalid parameters
- `ERR_SERVER_ERROR (6)`: Internal server error
- `ERR_NOT_OWNER (7)`: Operation requires ownership
- `ERR_USER_NOT_FOUND (8)`: User does not exist
- `ERR_SS_NOT_FOUND (9)`: Storage server unavailable
- `ERR_CONNECTION_FAILED (10)`: Network error

### Logging

All components log to `logs/<component>.log`:
- Timestamp for every operation
- IP addresses and ports
- Usernames and operations
- Success/failure status
- Error details

Example log entry:
```
[2025-10-28 16:30:45] Request: type=CREATE user=alice file=document.txt
[2025-10-28 16:30:45] File created: document.txt by alice on SS1
```

## Testing

### Manual Testing
```bash
# Run the provided test script
make test

# Or manually
chmod +x tests/run_tests.sh
./tests/run_tests.sh
```

### Network Analysis
```bash
# Monitor traffic with tcpdump
sudo tcpdump -i lo -X port 8080

# Or use Wireshark for GUI analysis
wireshark
```

### Testing with Netcat
```bash
# Connect to Name Server
nc localhost 8080

# Send raw data to test protocol
# (Note: Protocol uses length-prefixed binary messages)
```

## File Structure

```
DocsPlus/
├── bin/                    # Compiled executables
├── build/                  # Object files
├── data/                   # Persistent storage
│   ├── nameserver.db      # Name server database
│   └── storage_*/         # Storage server data
├── include/               # Header files
│   ├── common.h           # Common definitions
│   ├── nameserver.h       # Name server interface
│   ├── storageserver.h    # Storage server interface
│   ├── client.h           # Client interface
│   └── trie.h             # Trie data structure
├── logs/                  # Log files
├── src/                   # Source code
│   ├── common/            # Common utilities
│   ├── nameserver/        # Name server implementation
│   ├── storageserver/     # Storage server implementation
│   └── client/            # Client implementation
├── tests/                 # Test scripts
├── Makefile              # Build configuration
└── README.md             # This file
```

## Design Decisions

### Why C?
- Low-level control over network operations
- Efficient memory management
- POSIX socket API support
- Direct system call access for file operations

### Why SQLite?
- Embedded database (no separate server)
- ACID transactions for data consistency
- Efficient indexing and querying
- File-based persistence

### Why Sentence-Level Locking?
- Finer granularity than file-level locking
- Coarser than character-level (simpler, less overhead)
- Natural unit for text editing
- Good balance between concurrency and complexity

### Why Trie for File Search?
- O(m) lookup time (m = filename length)
- Better than hash table for prefix searches
- Cache-friendly for common prefixes
- Supports autocomplete features

## Known Limitations

1. **Single Name Server**: Name server is a single point of failure (could be addressed with leader election)
2. **Eventually Consistent Replication**: Replicas may lag behind primary
3. **No Byzantine Fault Tolerance**: Assumes honest servers
4. **Limited Checkpoint Storage**: Checkpoints consume disk space
5. **IPv4 Only**: No IPv6 support currently

## Future Enhancements

- [ ] Collaborative real-time editing (Operational Transformation)
- [ ] Conflict-free Replicated Data Types (CRDTs)
- [ ] End-to-end encryption
- [ ] Web-based client interface
- [ ] Name server replication and failover
- [ ] File compression
- [ ] Versioning with branching
- [ ] Rich text formatting
- [ ] Comments and annotations

## Performance Considerations

- **Concurrency**: pthread-based threading for handling multiple clients
- **Locking**: Fine-grained mutexes minimize contention
- **Caching**: Trie caches file lookups in Name Server
- **Connection Pooling**: Could be added for frequent operations
- **Batch Operations**: Multiple edits in single WRITE session

## Security Considerations

- **Authentication**: Basic username-based (production needs passwords/tokens)
- **Authorization**: Owner/read/write permissions enforced
- **Input Validation**: All inputs sanitized and validated
- **SQL Injection**: Prepared statements prevent SQL injection
- **Buffer Overflows**: Fixed-size buffers with bounds checking

## Troubleshooting

### Name Server won't start
- Check if port 8080 is already in use: `netstat -tuln | grep 8080`
- Ensure you have write permissions for `data/` and `logs/`

### Storage Server registration fails
- Verify Name Server is running first
- Check network connectivity: `ping localhost`
- Review logs in `logs/nameserver.log`

### Client can't connect
- Confirm Name Server is running
- Check firewall rules
- Verify IP address and port configuration

### File operations fail
- Check user permissions with INFO command
- Verify storage server is alive
- Review error messages in logs

### Lock timeout issues
- Another user may hold the lock
- Check active locks in Name Server logs
- Wait or contact file owner

## Contributing

This is an educational project demonstrating distributed systems concepts. Contributions welcome for:
- Bug fixes
- Performance improvements
- Additional features
- Documentation enhancements
- Test cases

## License

MIT License - See LICENSE file for details

## Authors

Built as a comprehensive distributed systems project implementing:
- Network programming with TCP sockets
- Concurrent programming with pthreads
- Database integration with SQLite
- Data structures (Trie)
- Client-server architecture
- Distributed file systems
- Access control systems

## References

- Google Docs architecture
- NFS (Network File System) design
- Operational Transformation algorithms
- Distributed consensus protocols
- POSIX socket programming
- SQLite documentation

---

**Version**: 1.0.0  
**Last Updated**: October 28, 2025
