# Security policy

## Reporting a problem

Please report a security problem **privately**, through GitHub:
[Security → Report a vulnerability](https://github.com/turjman/teknile/security/advisories/new).
Do not open a public issue for it.

Say what is affected (the device library, EVRe Studio, its API ports, the `evre` tools or the
Python package), the version, and how to repeat it. You will get an answer within a week.

## Supported versions

Fixes go into the latest version on `main`.

## Notes on use

- EVRe has no encryption. On a network, keep devices and the Studio's API ports (1219, 1220) on a
  trusted network or behind a tunnel.
- The Studio's API only reads unless writes are switched on in the window, and those switches are
  never saved.
- A device's login token is typed in the window and never stored by the Studio.

## Code signing policy

Free code signing provided by [SignPath.io](https://about.signpath.io), certificate by
[SignPath Foundation](https://signpath.org).

The project has applied to SignPath Foundation; until it is approved, the Windows files are not signed yet and this
policy describes how they will be.

### What is signed

| File | Signed |
|---|---|
| `EVReStudio.exe` | yes |
| `evre.exe`, `evre-sim.exe` | yes |
| `EVReStudio-<version>-setup.exe` | yes (the installer itself) |
| `EVReStudio-<version>-windows.zip` | the three programs inside it are signed |
| Qt&nbsp;and&nbsp;compiler&nbsp;runtime&nbsp;DLLs | no: unmodified upstream files from Qt's and MinGW's own builds |
| Linux&nbsp;AppImage&nbsp;and&nbsp;tools | no |

Only files built by this project from this repository are signed. Every signed file is built by GitHub Actions
(`.github/workflows/release.yml`) on GitHub-hosted runners from a release tag `vX.Y.Z`, and SignPath checks that it
comes from that build of this repository. A release is signed only after an approver has approved its signing
request by hand.

The signer shown by Windows is **SignPath Foundation**. To check a file: right-click it, Properties, Digital
Signatures; or in PowerShell `Get-AuthenticodeSignature .\EVReStudio-<version>-setup.exe`.

### Team and roles

| Role | Members |
|---|---|
| Committers&nbsp;and&nbsp;reviewers | [Ahmed Ragab](https://github.com/turjman) (maintainer): may change the source; reviews every pull request from anyone else before it is merged |
| Approvers | [Ahmed Ragab](https://github.com/turjman): approves each signing request |

All team members use multi-factor authentication for GitHub and SignPath. Build scripts and CI files
(`.github/workflows/`, `EVRe/studio/packaging/`, `CMakeLists.txt`) are reviewed like any other code.

### Privacy

This program will not transfer any information to other networked systems unless specifically requested by the
user or the person installing or operating it.

EVRe Studio and the `evre` tools connect only to the device, serial port or address the user gives. They send no
telemetry and do not check for updates. The Studio's API for other programs listens on this computer only
(127.0.0.1) unless the user chooses network access in the window. Listen uses USBPcap, which the user installs
separately; see [USBPcap](https://desowin.org/usbpcap/) for its terms.

### Removing it

The installer adds an uninstaller (Windows Settings, Apps, EVRe Studio, Uninstall). The zip and the AppImage install
nothing: delete the folder or the file. The Studio keeps its settings in the user's registry on Windows
(`HKEY_CURRENT_USER\Software\teknile\EVReStudio`) and in `~/.config/teknile/EVReStudio.conf` on Linux; delete them
to remove every trace.

### Reporting a signed file

If a signed file looks wrong (not from this repository, or doing what this policy says it does not), do not run it.
Report it privately as described in [Reporting a problem](#reporting-a-problem), with the file's SHA-256 hash and
where you got it. Misuse of the certificate can also be reported to SignPath at support@signpath.io.
