# Helwan Software Store

**A simple graphical package manager for Helwan Linux.**

Helwan Software Store provides a straightforward graphical interface for browsing, searching, installing, removing, and managing software packages on Helwan Linux.

It uses the **Pacman package manager and libalpm**, while keeping the interface simple and focused.

---

## For Everyday Users

### What can I do with Helwan Software Store?

Helwan Software Store lets you manage your software without having to type Pacman commands manually.

You can:

* Browse available packages.
* Search for packages by name or description.
* Filter packages by repository.
* Filter packages by category.
* See whether a package is:

  * Installed
  * Not Installed
  * Update Available
* Show only packages that have updates.
* Install software.
* Remove installed software.
* Refresh package databases.
* Upgrade the entire system.
* View package details.
* See operation progress while Pacman is working.

### Simple Workflow

1. Open **Helwan Software Store**.
2. Find the software you want.
3. Select the package.
4. Choose **Install** or **Remove**.
5. Confirm the operation when required.
6. The store runs the required system operation with administrator privileges.
7. The package list is refreshed after the operation.

No complicated package-management commands are required.

---

## Package Information

When you select a package, the store can show information such as:

* Package name
* Version
* Description
* Repository
* Installation status

This makes it easier to understand what you are about to install or remove.

---

## Updates

Helwan Software Store can identify packages for which a newer version is available in the configured repositories.

You can also enable the **Updates Only** filter to focus on packages with available updates.

For complete system maintenance, the **Upgrade System** operation runs:

```bash
pacman -Syu
```

---

# For Advanced Users

Helwan Software Store is not a replacement for the Arch Linux package system.

It is a graphical frontend built around the existing Helwan Linux package infrastructure.

## Backend

The store uses:

* **Pacman** for package operations.
* **libalpm** for package and repository information.
* **GTK3** for the graphical interface.
* **pthread** for background operations.

Administrative package operations are executed through:

```text
pkexec
```

This allows the system to request administrator authentication when required.

---

## Supported Operations

### Install

The store performs package installation using:

```bash
pacman -S --needed
```

The graphical frontend also uses non-interactive operation flags so that Pacman can perform the operation without requiring terminal input.

### Remove

Package removal uses:

```bash
pacman -R
```

### Refresh Databases

Repository databases can be refreshed using:

```bash
pacman -Sy
```

This operation refreshes the package databases only.

It is **not** a full system upgrade.

### Upgrade System

A complete system upgrade uses:

```bash
pacman -Syu
```

---

## Repository Support

The package list is obtained from the repositories configured for the system.

The store also reads the configured repository information from:

```text
/etc/pacman.conf
```

Repository filtering therefore follows the repositories available to the system rather than using a separate software source database.

---

## Package Detection

Package information is obtained through **libalpm**.

The store can determine whether a package is installed by checking the local Pacman database.

It can also compare the installed package version with versions available in configured sync repositories to detect an available update.

---

## Categories

The graphical category system currently uses package information and naming heuristics to group software into categories such as:

* Multimedia
* Games
* Development
* System Tools

These categories are a **frontend classification system** and are not additional Pacman repositories or official package metadata.

---

## Background Operations

Package operations are performed in a background thread so that the GTK interface remains responsive while Pacman is running.

The interface disables conflicting controls while an operation is in progress.

After the operation finishes, the package information is reloaded.

---

## Progress Reporting

The store reads Pacman's output while an operation is running and extracts percentage information when Pacman reports it.

The graphical progress bar therefore uses progress information coming from the actual Pacman process rather than an artificial animation.

Because Pacman's textual output varies between operations and stages, a percentage is not guaranteed to be available continuously for every operation.

---

## Security

Helwan Software Store does not attempt to implement its own package installation system.

Privileged package operations are delegated to:

```text
pkexec → pacman
```

Package management therefore remains under the control of the system's existing Pacman configuration and package databases.

---

# Requirements

A working Helwan Linux installation with:

* GTK3
* Pacman
* libalpm
* PolicyKit / `pkexec`
* pthread

is required.

---

# Building

The project is written in **C** and uses GTK3 and libalpm.

A typical build environment needs the development packages corresponding to:

```text
gtk3
libalpm
```

and the standard C build tools.

---

# Project Structure

The main components are:

```text
main.c
backend_alpm.c
backend_alpm.h
async_loader.c
core.h
ui_window.c
ui_about.c
ui.h
icon_manager.c
icon_manager.h
```

The backend handles communication with Pacman/libalpm, while the GTK frontend provides the graphical interface.

---

# Design Philosophy

Helwan Software Store follows a simple principle:

> **Use the existing Linux package infrastructure instead of reinventing it.**

The goal is to provide a practical graphical interface while keeping Pacman and libalpm at the core of package management.

No unnecessary package-management layer is introduced between the user interface and the existing system tools.

---

# Status

Helwan Software Store is under active development.

The current version focuses on the essential package-management workflow:

**Browse → Search → Inspect → Install / Remove → Update**

Additional functionality may be added over time without turning the application into an unnecessarily complicated package-management system.

---

# License

See the project's license file for the applicable licensing terms.

---

## Helwan Linux

Helwan Software Store is part of the **Helwan Linux** project.

**Website:** https://helwan-linux.github.io/helwanlinux/

**GitHub:** https://github.com/helwan-linux

**Email:** [helwanlinux@gmail.com](mailto:helwanlinux@gmail.com)
