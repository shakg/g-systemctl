package services

import (
	"fmt"
	"os/exec"
	"runtime"
	"strings"
)

type ServiceUnit struct {
	Unit        string `json:"unit"`
	Load        string `json:"load"`
	Active      string `json:"active"`
	Sub         string `json:"sub"`
	Description string `json:"description"`
}

func ListServices() ([]ServiceUnit, error) {
	if isMacOS() {
		cmd := exec.Command("launchctl", "list")
		println("Executing command:", cmd.String())
		output, err := cmd.Output()
		if err != nil {
			return nil, err
		}
		return parseLaunchctlOutput(string(output)), nil
	}

	cmd := exec.Command("systemctl", "list-units", "-t", "service", "--full", "--all", "--plain", "--no-legend", "--no-pager")
	println("Executing command:", cmd.String())
	output, err := cmd.Output()
	if err != nil {
		return nil, err
	}

	return parseSystemctlOutput(string(output)), nil
}

func parseLaunchctlOutput(s string) []ServiceUnit {
	lines := strings.Split(strings.TrimSpace(s), "\n")
	var units []ServiceUnit

	// Skip header line
	if len(lines) > 0 {
		lines = lines[1:]
	}

	for _, line := range lines {
		fields := strings.Fields(line)
		fmt.Println(fields)
		if len(fields) < 3 {
			continue
		}

		// launchctl list output format:
		// PID     Status  Label
		unit := ServiceUnit{
			Unit: fields[2], // Label is the service name
			Sub:  "stopped",
		}

		// If PID is "-", service is stopped
		// If PID is a number, service is running
		if fields[0] != "-" {
			unit.Sub = "running"
		}

		// Set other fields with reasonable defaults
		unit.Load = "loaded"
		unit.Active = unit.Sub
		unit.Description = unit.Unit // Use service name as description

		units = append(units, unit)
	}

	return units
}

func isMacOS() bool {
	return runtime.GOOS == "darwin"
}

func ToggleService(name string, start bool) error {
	action := "stop"
	if start {
		action = "start"
	}

	cmd := exec.Command("systemctl", action, name)
	return cmd.Run()
}

func parseSystemctlOutput(output string) []ServiceUnit {
	lines := strings.Split(strings.TrimSpace(output), "\n")
	var units []ServiceUnit

	for _, line := range lines {
		fields := strings.Fields(line)
		if len(fields) < 5 {
			continue
		}

		unit := ServiceUnit{
			Unit:   fields[0],
			Load:   fields[1],
			Active: fields[2],
			Sub:    fields[3],
		}

		if len(fields) > 4 {
			unit.Description = strings.Join(fields[4:], " ")
		}

		units = append(units, unit)
	}

	return units
}
