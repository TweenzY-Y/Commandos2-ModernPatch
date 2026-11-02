# Security Policy

## Supported versions

Only the latest release receives fixes. Please update to the newest version from the
[Releases](../../releases) page before reporting a problem.

## Scope

Commandos2-ModernPatch is a `ddraw.dll` loaded by the game. It patches the running game in
memory, reads `Commandos2-ModernPatch.ini` and writes `Commandos2-ModernPatch.log` in the
game folder. It does not use the network, does not run with elevated rights and does not
change any game files on disk. Release packages are built from this repository by
`package.ps1`, which downloads cnc-ddraw from its official GitHub releases and verifies its
checksum.

Relevant security issues include, for example, code or files in a release package that do
not match the source, a way for the configuration file or game data to make the patch run
unintended code, or the bundled cnc-ddraw version having a known vulnerability.

## Reporting a vulnerability

Please **do not** open a public issue for security problems. Report them privately through
GitHub: open the repository's **Security** tab and choose **Report a vulnerability**.
Include the patch version, what you found and how to reproduce it.

You can expect a first answer within a few days. Fixed issues are published in the release
notes once a new version is available.

If a release package was flagged by an antivirus program, that is usually a false positive
caused by the proxy `ddraw.dll`; please open a normal issue with the name of the antivirus
and the detection.
