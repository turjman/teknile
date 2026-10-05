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
