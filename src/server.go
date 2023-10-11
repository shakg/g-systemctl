package main

import (
	"os"
	"encoding/json"
	"fmt"
	"net/http"
	"os/exec"
	"strings"
	"embed"

)

type ServiceUnit struct {
	Unit        string `json:"unit"`
	Load        string `json:"load"`
	Active      string `json:"active"`
	Sub         string `json:"sub"`
	Description string `json:"description"`
}

	
//go:embed *.html *.js
var content embed.FS


func main() {
	if len(os.Args) < 3 {
		fmt.Println("Usage: ./server <PORT> <sudo_password>")
		os.Exit(1)
	}

	port := os.Args[1]
	password := os.Args[2]
	

	// Create a handler function that wraps the file server
	http.Handle("/", http.StripPrefix("/",  http.FileServer(http.FS(content))))

	http.HandleFunc("/api", func(w http.ResponseWriter, r *http.Request) {
		// Run systemctl list-units and capture its output
		output, err := runCommand("systemctl list-units -t service --full --all --plain --no-legend --no-pager")
		if err != nil {
			http.Error(w, "Error running systemctl", http.StatusInternalServerError)
			return
		}

		// Parse the output and convert it to a JSON array
		serviceUnits := parseSystemctlOutput(output)

		// Convert the data to JSON and write it as the HTTP response
		jsonData, err := json.Marshal(serviceUnits)
		if err != nil {
			http.Error(w, "Error marshaling to JSON", http.StatusInternalServerError)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		w.Write(jsonData)
	})

	http.HandleFunc("/service", func(w http.ResponseWriter, r *http.Request) {
		open := r.URL.Query().Get("open")
		serviceName := r.URL.Query().Get("service_name")

		if open == "true" {
			fmt.Fprintf(w, "Trying to open Service %s", serviceName)

			// to pass password to command in linux, we use this , echo "your_password" | sudo -S your_command
			pre_command := "systemctl start " + serviceName
			c := exec.Command("sh", "-c", "echo "+password+" | sudo -S -k " + pre_command)
			err := c.Run()
			if err != nil {
				fmt.Printf("Error: %s\n", err)
				http.Error(w, "Error in running command", http.StatusBadRequest)
			}
			
		} else if open == "false" {
			fmt.Fprintf(w, "Trying to close Service %s", serviceName)
			
			pre_command := "systemctl stop " + serviceName
			c := exec.Command("sh", "-c", "echo "+password+" | sudo -S -k " + pre_command)
			err := c.Run()
			if err != nil {
				fmt.Printf("Error: %s\n", err)
				http.Error(w, "Error in running command", http.StatusBadRequest)
			}
/**
Trying to open Service docker.serviceError is exec: "echo 1 | systemctl start docker.service": executable file not found in $PATHError in running command

*/
		} else {
			http.Error(w, "Invalid 'open' parameter. Use 'true' or 'false'.", http.StatusBadRequest)
		}
	})

	fmt.Printf("Server is listening on port %s...\n", port)
	http.ListenAndServe(fmt.Sprintf(":%s", port), nil)
}

func runCommand(cmd string) (string, error) {
	output := ""
	// Execute the command
	cmdParts := strings.Fields(cmd)
	c := exec.Command(cmdParts[0], cmdParts[1:]...)
	stdout, err := c.StdoutPipe()
	if err != nil {
		return output, err
	}
	err = c.Start()
	if err != nil {
		return output, err
	}
	// Read the command's output
	buf := make([]byte, 4096)
	for {
		n, err := stdout.Read(buf)
		if err != nil {
			break
		}
		output += string(buf[:n])
	}
	err = c.Wait()
	if err != nil {
		return output, err
	}
	return output, nil
}

func parseSystemctlOutput(output string) []ServiceUnit {
	lines := strings.Split(output, "\n")
	var serviceUnits []ServiceUnit

	for _, line := range lines {
		fields := strings.Fields(line)
		if len(fields) >= 5 {
			unit := ServiceUnit{
				Unit:        fields[0],
				Load:        fields[1],
				Active:      fields[2],
				Sub:         fields[3],
				Description: fields[4],
			}
			serviceUnits = append(serviceUnits, unit)
		}
	}

	return serviceUnits
}

