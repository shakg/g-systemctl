package main

import (
	"encoding/json"
	"fmt"
	"net/http"
	"os/exec"
	"strings"
)

type ServiceUnit struct {
	Unit        string `json:"unit"`
	Load        string `json:"load"`
	Active      string `json:"active"`
	Sub         string `json:"sub"`
	Description string `json:"description"`
}

func main() {
	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
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

	port := 8080
	fmt.Printf("Server is listening on port %d...\n", port)
	http.ListenAndServe(fmt.Sprintf(":%d", port), nil)
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

