# g-systemctl User Stories

This document contains user stories for the g-systemctl terminal user interface application. Stories are organized by epic and follow the Gherkin format for acceptance criteria.

---

## Epic: Service Discovery & Viewing

### Story SVC-001: View All System Services [DONE]

**Narrative**: As a system administrator, I want to view a list of all system services with their current status, so that I can quickly understand what services are running on my system.

**Gherkin Scenarios**:

**Scenario: Display service list on application startup**
```gherkin
Given the user launches g-systemctl
And the system has services managed by systemctl (Linux) or launchctl (macOS)
When the application initializes
Then all system services are displayed in a scrollable list
And each service shows its name, status (running/stopped), and description
And running services display a visual indicator distinguishing them from stopped services
```

**Scenario: Display service count in status bar**
```gherkin
Given the user launches g-systemctl
When services are loaded successfully
Then the status bar displays "Loaded X services" where X is the total count
```

**Priority**: Must Have  
**Dependencies**: None (core functionality)

**Definition of Done**:
- [ ] All Gherkin scenarios verified passing
- [ ] Service list renders correctly on Linux with systemctl
- [ ] Service list renders correctly on macOS with launchctl
- [ ] Status indicators visually distinguish running vs stopped services
- [ ] Unit tests for service parsing logic
- [ ] Integration tests for platform-specific service managers
- [ ] Code review completed

---

### Story SVC-002: Navigate Service List with Keyboard [DONE]

**Narrative**: As a system administrator, I want to navigate the service list using keyboard shortcuts, so that I can efficiently browse services without leaving the terminal.

**Gherkin Scenarios**:

**Scenario: Navigate down through service list**
```gherkin
Given the service list is displayed
And service at index 0 is currently selected
When I press the Down arrow key or 'j'
Then the selection moves to service at index 1
And the previously selected service is no longer highlighted
And the newly selected service is visually highlighted
```

**Scenario: Navigate up through service list**
```gherkin
Given the service list is displayed
And service at index 5 is currently selected
When I press the Up arrow key or 'k'
Then the selection moves to service at index 4
And the newly selected service is visually highlighted
```

**Priority**: Must Have  
**Dependencies**: SVC-001

**Definition of Done**:
- [ ] All Gherkin scenarios verified passing
- [ ] Both arrow keys and vim-style (j/k) navigation work
- [ ] Visual feedback clearly indicates selected service
- [ ] Boundary conditions handled without errors
- [ ] Unit tests for navigation logic
- [ ] Code review completed

---

### Story SVC-003: Filter Services by Name [DONE]

**Narrative**: As a system administrator, I want to filter services by typing a search term, so that I can quickly locate specific services in a large list.

**Gherkin Scenarios**:

**Scenario: Filter services by partial name match**
```gherkin
Given the service list is displayed with services including "docker.service", "ssh.service", "nginx.service"
When I type "dock"
Then only services containing "dock" in their name are displayed
And the filter text "dock" is shown in the filter box
And the service count updates to reflect filtered results
```

**Scenario: Filter is case-insensitive**
```gherkin
Given the service list includes "Docker.service"
When I type "docker" (lowercase)
Then "Docker.service" appears in the filtered results
```

**Scenario: Clear filter character by character**
```gherkin
Given I have typed "nginx" in the filter
And only nginx-related services are displayed
When I press Backspace
Then the filter text becomes "ngin"
And services matching "ngin" are displayed
```

**Scenario: Empty filter shows all services**
```gherkin
Given I have an active filter showing 5 of 100 services
When I clear all filter characters using Backspace
Then all 100 services are displayed
And the filter box shows "(type to filter)" placeholder
```

**Priority**: Must Have  
**Dependencies**: SVC-001

**Definition of Done**:
- [ ] All Gherkin scenarios verified passing
- [ ] Real-time filtering as user types
- [ ] Case-insensitive matching implemented
- [ ] Filter state visually indicated
- [ ] Selection index adjusts when filter changes
- [ ] Unit tests for filter logic
- [ ] Code review completed

---

## Epic: Service Control

### Story CTL-001: Start a Stopped Service [DONE]

**Narrative**: As a system administrator, I want to start a stopped service by pressing Enter, so that I can quickly bring services online without typing systemctl commands.

**Gherkin Scenarios**:

**Scenario: Start a stopped service successfully**
```gherkin
Given service "nginx.service" is currently stopped
And "nginx.service" is selected in the list
When I press Enter
Then the system executes the appropriate start command (systemctl start / launchctl load)
And a status message "Successfully toggled nginx.service" is displayed
And the service list refreshes automatically
And "nginx.service" now shows as running
```

**Scenario: Start service requires elevated privileges**
```gherkin
Given I am running g-systemctl without root privileges
And a stopped service is selected
When I press Enter to start the service
Then the system attempts to use sudo for the start command
And if sudo succeeds, the service starts
And the status updates accordingly
```

**Priority**: Must Have  
**Dependencies**: SVC-001, SVC-002

**Definition of Done**:
- [ ] All Gherkin scenarios verified passing
- [ ] Linux systemctl start command integration working
- [ ] macOS launchctl load command integration working
- [ ] Sudo/privilege escalation handled appropriately
- [ ] Error messages displayed clearly
- [ ] Service list auto-refreshes after action
- [ ] Unit tests for start service logic
- [ ] Integration tests with mock command executor
- [ ] Code review completed

---

### Story CTL-002: Stop a Running Service [DONE]

**Narrative**: As a system administrator, I want to stop a running service by pressing Enter, so that I can quickly take services offline for maintenance or troubleshooting.

**Gherkin Scenarios**:

**Scenario: Stop a running service successfully**
```gherkin
Given service "nginx.service" is currently running
And "nginx.service" is selected in the list
When I press Enter
Then the system executes the appropriate stop command (systemctl stop / launchctl unload)
And a status message "Successfully toggled nginx.service" is displayed
And the service list refreshes automatically
And "nginx.service" now shows as stopped
```

**Priority**: Must Have  
**Dependencies**: CTL-001

**Definition of Done**:
- [ ] All Gherkin scenarios verified passing
- [ ] Linux systemctl stop command integration working
- [ ] macOS launchctl unload command integration working
- [ ] Error handling for failed stop operations
- [ ] Unit tests for stop service logic
- [ ] Code review completed

---

### Story CTL-003: Refresh Service List [DONE]

**Narrative**: As a system administrator, I want to refresh the service list by pressing 'r', so that I can see the current state of services after external changes.

**Gherkin Scenarios**:

**Scenario: Refresh updates service states**
```gherkin
Given the service list is displayed
And a service was started externally via command line
When I press 'r'
Then the service list reloads from the system
And the newly started service shows as running
And a status message "Loaded X services" is displayed
```

**Scenario: Refresh preserves filter**
```gherkin
Given I have an active filter "nginx"
And 3 services are displayed
When I press 'r' to refresh
Then the service data is reloaded
And the filter "nginx" remains active
And only nginx-related services are displayed
```

**Priority**: Must Have  
**Dependencies**: SVC-001

**Definition of Done**:
- [ ] All Gherkin scenarios verified passing
- [ ] Refresh preserves current filter state
- [ ] Selection position maintained where possible
- [ ] Error states handled gracefully
- [ ] Unit tests for refresh logic
- [ ] Code review completed

---

## Epic: User Interface & Experience

### Story UI-001: Display Help Screen [DONE]

**Narrative**: As a new user, I want to view a help screen showing all keyboard shortcuts, so that I can learn how to use the application effectively.

**Gherkin Scenarios**:

**Scenario: Show help screen**
```gherkin
Given the main service list is displayed
When I press '?'
Then a help overlay is displayed
And the help shows all keyboard shortcuts:
  | Key        | Action                          |
  | Up/k       | Move selection up               |
  | Down/j     | Move selection down             |
  | Enter      | Toggle selected service         |
  | r          | Refresh service list            |
  | ?          | Toggle help screen              |
  | q/Esc      | Quit                            |
  | Type       | Filter services                 |
  | Backspace  | Delete filter character         |
```

**Scenario: Hide help screen**
```gherkin
Given the help screen is displayed
When I press '?'
Then the help screen is hidden
And the main service list is visible again
```

**Priority**: Should Have  
**Dependencies**: None

**Definition of Done**:
- [ ] All Gherkin scenarios verified passing
- [ ] Help content is accurate and complete
- [ ] Help overlay is centered and readable
- [ ] Toggle behavior works correctly
- [ ] Code review completed

---

### Story UI-002: Exit Application [DONE]

**Narrative**: As a user, I want to exit the application using 'q' or Escape, so that I can return to my terminal session.

**Gherkin Scenarios**:

**Scenario: Exit with 'q' key**
```gherkin
Given g-systemctl is running
When I press 'q'
Then the application exits cleanly
And the terminal is restored to its previous state
And exit code is 0
```

**Scenario: Exit with Escape key**
```gherkin
Given g-systemctl is running
When I press Escape
Then the application exits cleanly
And the terminal is restored to its previous state
```

**Priority**: Must Have  
**Dependencies**: None

**Definition of Done**:
- [ ] All Gherkin scenarios verified passing
- [ ] Terminal state properly restored on exit
- [ ] Both 'q' and Escape keys work
- [ ] Works from any application state
- [ ] Code review completed

---

### Story UI-003: Display Status and Error Messages [DONE]

**Narrative**: As a user, I want to see status messages and errors in a dedicated status bar, so that I have feedback on my actions and any issues that occur.

**Gherkin Scenarios**:

**Scenario: Display success status message**
```gherkin
Given I have just toggled a service successfully
Then the status bar displays "Successfully toggled <service_name>"
And the message is visible until the next action
```

**Scenario: Display error message with distinctive styling**
```gherkin
Given a service operation has failed
Then the status bar displays the error message
And the error text is styled in red/error color
And the error message includes relevant details
```

**Scenario: Display keyboard hints in status bar**
```gherkin
Given the main view is displayed
And no recent status message exists
Then the status bar shows "q:quit r:refresh Enter:toggle"
```

**Priority**: Should Have  
**Dependencies**: None

**Definition of Done**:
- [ ] All Gherkin scenarios verified passing
- [ ] Success and error messages visually distinct
- [ ] Status bar consistently positioned
- [ ] Messages are informative but concise
- [ ] Code review completed

---

## Epic: Cross-Platform Support

### Story PLT-001: Linux systemctl Integration [DONE]

**Narrative**: As a Linux system administrator, I want g-systemctl to use systemctl commands, so that I can manage systemd services natively.

**Gherkin Scenarios**:

**Scenario: List services using systemctl**
```gherkin
Given I am running on a Linux system with systemd
When g-systemctl lists services
Then it executes "systemctl list-units -t service --full --all --plain --no-legend --no-pager"
And parses the output to extract unit, load, active, sub, and description fields
```

**Scenario: Start service using systemctl**
```gherkin
Given I am on Linux
And service "nginx.service" is selected
When I trigger start action
Then the command "systemctl start nginx.service" is executed
And sudo is used if not running as root
```

**Scenario: Stop service using systemctl**
```gherkin
Given I am on Linux
And service "nginx.service" is selected and running
When I trigger stop action
Then the command "systemctl stop nginx.service" is executed
```

**Priority**: Must Have  
**Dependencies**: None

**Definition of Done**:
- [ ] All Gherkin scenarios verified passing
- [ ] Correct systemctl commands executed
- [ ] Output parsing handles all service states
- [ ] Unit tests for LinuxServiceManager
- [ ] Tested on Ubuntu 22.04+ and similar distributions
- [ ] Code review completed

---

### Story PLT-002: macOS launchctl Integration [DONE]

**Narrative**: As a macOS system administrator, I want g-systemctl to use launchctl commands, so that I can manage launchd services natively.

**Gherkin Scenarios**:

**Scenario: List services using launchctl**
```gherkin
Given I am running on macOS
When g-systemctl lists services
Then it executes "launchctl list"
And parses the output to extract PID, status, and label fields
And running services (with non-"-" PID) are marked as running
```

**Scenario: Start service using launchctl**
```gherkin
Given I am on macOS
And service "com.apple.example" is selected
When I trigger start action
Then the command "launchctl load com.apple.example" is executed
```

**Scenario: Stop service using launchctl**
```gherkin
Given I am on macOS
And service "com.apple.example" is running
When I trigger stop action
Then the command "launchctl unload com.apple.example" is executed
```

**Priority**: Must Have  
**Dependencies**: None

**Definition of Done**:
- [ ] All Gherkin scenarios verified passing
- [ ] Correct launchctl commands executed
- [ ] Output parsing handles launchctl format
- [ ] Unit tests for MacOSServiceManager
- [ ] Tested on macOS 12+
- [ ] Code review completed

---

### Story PLT-003: Platform Detection [DONE]

**Narrative**: As a user, I want g-systemctl to automatically detect my operating system, so that it uses the appropriate service manager without manual configuration.

**Gherkin Scenarios**:

**Scenario: Detect Linux platform**
```gherkin
Given the application is compiled on Linux
When platform detection runs
Then Platform::Linux is returned
And LinuxServiceManager is instantiated
```

**Scenario: Detect macOS platform**
```gherkin
Given the application is compiled on macOS
When platform detection runs
Then Platform::MacOS is returned
And MacOSServiceManager is instantiated
```

**Scenario: Handle unsupported platform**
```gherkin
Given the application is run on an unsupported OS
When platform detection runs
Then Platform::Unknown is returned
And an appropriate error message is displayed
And the application exits gracefully
```

**Priority**: Must Have  
**Dependencies**: PLT-001, PLT-002

**Definition of Done**:
- [ ] All Gherkin scenarios verified passing
- [ ] Compile-time platform detection working
- [ ] Correct service manager factory selection
- [ ] Graceful handling of unsupported platforms
- [ ] Code review completed

---

## Epic: Command Line Interface

### Story CLI-001: Display Help via Command Line [DONE]

**Narrative**: As a user, I want to see help information by running `g-systemctl --help`, so that I can understand usage before launching the TUI.

**Gherkin Scenarios**:

**Scenario: Show help with --help flag**
```gherkin
Given I run "g-systemctl --help"
Then usage information is printed to stdout
And available options are listed
And the application exits with code 0
And the TUI is not launched
```

**Scenario: Show help with -h flag**
```gherkin
Given I run "g-systemctl -h"
Then the same help information is displayed as --help
```

**Priority**: Should Have  
**Dependencies**: None

**Definition of Done**:
- [ ] All Gherkin scenarios verified passing
- [ ] Help text is accurate and well-formatted
- [ ] Both -h and --help flags work
- [ ] Code review completed

---

### Story CLI-002: Display Version Information [DONE]

**Narrative**: As a user, I want to check the application version by running `g-systemctl --version`, so that I can verify which version is installed.

**Gherkin Scenarios**:

**Scenario: Show version with --version flag**
```gherkin
Given I run "g-systemctl --version"
Then version information is printed including:
  | Field    | Example Value |
  | Version  | 1.0.0         |
  | Platform | Linux/macOS   |
And the application exits with code 0
```

**Scenario: Show version with -v flag**
```gherkin
Given I run "g-systemctl -v"
Then the same version information is displayed as --version
```

**Priority**: Should Have  
**Dependencies**: None

**Definition of Done**:
- [ ] All Gherkin scenarios verified passing
- [ ] Version matches CMakeLists.txt version
- [ ] Platform is correctly detected and displayed
- [ ] Code review completed

---

## Epic: Future Enhancements

### Story ENH-001: Service Details Panel

**Narrative**: As a system administrator, I want to view detailed information about a selected service, so that I can understand its configuration and dependencies without leaving the TUI.

**Gherkin Scenarios**:

**Scenario: View service details**
```gherkin
Given service "nginx.service" is selected
When I press 'd' or Enter+Shift (detail key TBD)
Then a detail panel is displayed showing:
  | Field        | Description                    |
  | Unit Name    | Full service unit name         |
  | Description  | Service description            |
  | Load State   | loaded/not-found/masked        |
  | Active State | active/inactive/failed         |
  | Sub State    | running/dead/failed/exited     |
  | PID          | Process ID (if running)        |
And I can dismiss the panel to return to the list
```

**Scenario: View service dependencies (Linux)**
```gherkin
Given I am viewing details for "nginx.service" on Linux
When I view the dependencies section
Then services that nginx depends on (Requires, Wants) are listed
And services that depend on nginx (RequiredBy, WantedBy) are listed
```

**Priority**: Could Have  
**Dependencies**: SVC-001, PLT-001, PLT-002

**Definition of Done**:
- [ ] All Gherkin scenarios verified passing
- [ ] Detail panel renders correctly
- [ ] Linux-specific details retrieved via systemctl show
- [ ] macOS-specific details retrieved appropriately
- [ ] Panel is dismissible
- [ ] Unit tests for detail parsing
- [ ] Code review completed

---

### Story ENH-002: Enable/Disable Service Auto-Start

**Narrative**: As a system administrator, I want to enable or disable a service's auto-start behavior, so that I can control which services start at boot.

**Gherkin Scenarios**:

**Scenario: Enable service auto-start (Linux)**
```gherkin
Given service "nginx.service" is disabled
And the service is selected
When I press 'e' to enable
Then "systemctl enable nginx.service" is executed
And a confirmation message is displayed
```

**Scenario: Disable service auto-start (Linux)**
```gherkin
Given service "nginx.service" is enabled
And the service is selected
When I press 'e' to toggle enable state
Then "systemctl disable nginx.service" is executed
And a confirmation message is displayed
```

**Scenario: Show enabled state indicator**
```gherkin
Given the service list is displayed
Then each service shows whether it is enabled for auto-start
And enabled services have a visual indicator (e.g., [E] or icon)
```

**Priority**: Could Have  
**Dependencies**: CTL-001, PLT-001

**Definition of Done**:
- [ ] All Gherkin scenarios verified passing
- [ ] Enable/disable commands work on Linux
- [ ] Enabled state visible in service list
- [ ] macOS equivalent implemented (if applicable)
- [ ] Unit tests for enable/disable logic
- [ ] Code review completed

---

### Story ENH-003: Service Log Viewer

**Narrative**: As a system administrator, I want to view recent logs for a selected service, so that I can troubleshoot issues without opening a separate terminal.

**Gherkin Scenarios**:

**Scenario: View recent service logs (Linux)**
```gherkin
Given service "nginx.service" is selected
When I press 'l' to view logs
Then recent journal entries for nginx.service are displayed
And logs are shown in a scrollable panel
And I can dismiss the panel to return to the list
```

**Scenario: Refresh logs**
```gherkin
Given the log panel is displayed
When I press 'r'
Then the log content is refreshed with latest entries
```

**Priority**: Could Have  
**Dependencies**: SVC-001, PLT-001

**Definition of Done**:
- [ ] All Gherkin scenarios verified passing
- [ ] Log panel renders correctly
- [ ] journalctl integration for Linux
- [ ] Log scrolling works
- [ ] Panel is dismissible
- [ ] Unit tests for log retrieval
- [ ] Code review completed

---

### Story ENH-004: Confirmation Dialog for Critical Actions

**Narrative**: As a system administrator, I want to confirm before stopping critical services, so that I don't accidentally disrupt important system functions.

**Gherkin Scenarios**:

**Scenario: Confirm before stopping critical service**
```gherkin
Given service "sshd.service" is selected (marked as critical)
When I press Enter to stop it
Then a confirmation dialog appears: "Stop sshd.service? This may affect remote access. [y/N]"
And the action only proceeds if I press 'y'
And pressing 'n' or Escape cancels the action
```

**Scenario: Regular services don't require confirmation**
```gherkin
Given service "nginx.service" is selected (not marked critical)
When I press Enter to toggle it
Then the action proceeds immediately without confirmation
```

**Priority**: Should Have  
**Dependencies**: CTL-001, CTL-002

**Definition of Done**:
- [ ] All Gherkin scenarios verified passing
- [ ] Confirmation dialog renders correctly
- [ ] Critical services identified appropriately
- [ ] Y/N/Escape handling works
- [ ] Unit tests for confirmation logic
- [ ] Code review completed

---

### Story ENH-005: Search with Regular Expressions

**Narrative**: As a power user, I want to filter services using regular expressions, so that I can create complex search patterns.

**Gherkin Scenarios**:

**Scenario: Enable regex mode**
```gherkin
Given the main view is displayed
When I press '/' to enter regex search mode
Then the filter box indicates regex mode (e.g., "Regex: ")
And typed patterns are interpreted as regular expressions
```

**Scenario: Filter with regex pattern**
```gherkin
Given regex mode is enabled
When I type "^nginx.*\\.service$"
Then only services matching the regex are displayed
```

**Scenario: Handle invalid regex gracefully**
```gherkin
Given regex mode is enabled
When I type "[invalid"
Then an error indicator shows the regex is invalid
And no services are filtered out (or all are hidden with error message)
```

**Priority**: Could Have  
**Dependencies**: SVC-003

**Definition of Done**:
- [ ] All Gherkin scenarios verified passing
- [ ] Regex mode toggle works
- [ ] Valid regex patterns filter correctly
- [ ] Invalid regex handled gracefully
- [ ] Unit tests for regex filtering
- [ ] Code review completed

---

## Dependency Summary

| From | To | Type | Notes |
|------|-----|------|-------|
| SVC-002 | SVC-001 | Logical | Navigation requires service list |
| SVC-003 | SVC-001 | Logical | Filtering requires service list |
| CTL-001 | SVC-001, SVC-002 | Logical | Start action requires list and selection |
| CTL-002 | CTL-001 | Logical | Stop mirrors start implementation |
| CTL-003 | SVC-001 | Logical | Refresh reloads service list |
| PLT-003 | PLT-001, PLT-002 | Technical | Factory requires both implementations |
| ENH-001 | SVC-001, PLT-001, PLT-002 | Logical | Details extend platform managers |
| ENH-002 | CTL-001, PLT-001 | Logical | Enable/disable uses similar patterns |
| ENH-003 | SVC-001, PLT-001 | Technical | Logs require journalctl integration |
| ENH-004 | CTL-001, CTL-002 | Logical | Confirmation wraps existing actions |
| ENH-005 | SVC-003 | Logical | Regex extends existing filter |

---

## Recommended Implementation Order

### Phase 1: Core Functionality (Must Have)
1. PLT-001 - Linux systemctl Integration
2. PLT-002 - macOS launchctl Integration  
3. PLT-003 - Platform Detection
4. SVC-001 - View All System Services
5. SVC-002 - Navigate Service List
6. SVC-003 - Filter Services by Name
7. CTL-001 - Start a Stopped Service
8. CTL-002 - Stop a Running Service
9. CTL-003 - Refresh Service List
10. UI-002 - Exit Application

### Phase 2: Polish (Should Have)
1. UI-001 - Display Help Screen
2. UI-003 - Display Status and Error Messages
3. CLI-001 - Display Help via Command Line
4. CLI-002 - Display Version Information
5. ENH-004 - Confirmation Dialog for Critical Actions

### Phase 3: Enhancements (Could Have)
1. ENH-001 - Service Details Panel
2. ENH-002 - Enable/Disable Service Auto-Start
3. ENH-003 - Service Log Viewer
4. ENH-005 - Search with Regular Expressions

