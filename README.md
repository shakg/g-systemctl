# G-Systemctl

g-systemctl is a graphical user interface for your services in *nix systems. you can see all of your service deamons and search, filter them. 

![Screenshot from 2023-10-15 23-51-45](https://github.com/shakg/g-systemctl/assets/76657662/f9053cba-46a1-4124-838b-12a35f277b20)


## Table of Contents

- [Prerequisites](#prerequisites)
- [Installation](#installation)
- [Usage](#usage)
- [API](#api)
- [Frontend](#frontend)
- [Contributing](#contributing)

### Prerequisites

- Go (Golang): [Install Go](https://golang.org/doc/install)
- A web browser for frontend testing

### Installation

1. Clone the repository:

   ```bash
   git clone https://github.com/shakg/g-systemctl.git
   cd g-systemctl
   ```

2. Build the project:

   ```bash
   go build src/server.go
   ```
## One Line Relase Download

```bash
curl https://api.github.com/repos/shakg/g-systemctl/releases/latest |jq '.assets[0].browser_download_url' | xargs wget
```


## Usage

```bash
chmod +x server
```
and 

```bash
./server <port>
```
<port> could be any free port in your system. For example 8080. 

### Running the Server

To start the server, run the executable or use `go run` with the following command:

```bash
./server <port>
# or
go run server.go <port> 
```

Replace `<port>` with the desired port number (e.g., 8080).

> ! IMPORTANT, run with sudo if password needed

The server will be accessible at `http://localhost:<port>`.

## API

The API endpoint provides information about the running system's status.

- **Endpoint:** `/api`
- **Method:** `GET`

Example response:

```json
[
   {
      "unit":"accounts-daemon.service",
      "load":"loaded",
      "active":"active",
      "sub":"running",
      "description":"Accounts"
   },
   {
      "unit":"acpid.service",
      "load":"loaded",
      "active":"active",
      "sub":"running",
      "description":"ACPI"
   }
]
```
## Service

The service endpoint enables us to start and stop services.

- **Endpoint:** `/service`
- **Method:** `GET`

Example Usage:

```
/service?open=true&service_name=MyService
```


## Frontend

The frontend of this project consists of vanilla HTML and JavaScript. The frontend files are served at the root path ("/").

- **HTML File:** `index.html`
- **JavaScript File:** `app.js`

You can access the frontend at `http://localhost:<port>`.

## Contributing

Contributions are welcome! Please follow the [Contributing Guidelines](CONTRIBUTING.md) for details on how to contribute to this project.
