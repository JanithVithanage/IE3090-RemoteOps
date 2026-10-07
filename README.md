# RemoteOps – IE3090 Assignment

A client-server remote operations system implemented in C for the IE3090 assignment.

The project consists of an **Agent** server and a **Controller** client communicating over TCP, with a separate UDP channel for periodic system monitoring.

---

## Student Information

| Item | Value |
|---|---|
| Student Registration Number | **IT24102851** |
| Session ID (SID) | **SID:1582** |
| Authentication Token | **OPS-2851** |
| Agent TCP Port | **9410** |
| UDP Monitoring Test Port | **9001** |
| File Storage Path | `./agentfiles/IT24102851/` |
| Log File | `remoteops_IT24102851.log` |

---

## Project Structure

```text
IE3090-RemoteOps/
│
├── agent_851.c
├── controller_851.c
├── Makefile_851
├── README.md
├── .gitignore
│
└── Runtime files
    ├── agent_851
    ├── controller_851
    ├── agentfiles/
    └── remoteops_IT24102851.log
