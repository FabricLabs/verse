# Fabric
## Transport
### Encryption
Fabric uses the NOISE protocol to encrypt all communications with strong forward secrecy.  Connections may be initiated over TCP to a known peer as follows:

```
tcp:<PublicKey>@<HostName>:<HostPort>
```

## Messages
```
magic (4 bytes):
version (4 bytes):
parent (32 bytes?):
author (32 bytes?):
type (4 bytes?):
size (4 || > 0):
hash (sha256 of body):
signature (64 bytes):
body (length defined by size field): optional
````

### Message Types
```
00: undefined
01: undefined
09: undefined
0A: undefined
10: undefined
100: undefined
1000: undefined
1024: GenericMessage
2048: ApplicationMessage
4096: NetworkMessage
8192: Kill
A000: undefined
B000: Blob
```
