# Section 2 – FIFO Chat

**WSL نکته:** mkfifo روی `/mnt/*` کار نمی‌کند. در `/tmp` اجرا کنید.

Server:
```
./server room1
```
Clients:
```
./client alice room1
./client bob room1
```
