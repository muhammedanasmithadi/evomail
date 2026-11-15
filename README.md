# evomail - a CLI for GNOME Evolution

evomail reads mail through Evolution's own account configuration and mail
stack. It talks to Evolution Data Server (EDS) the way Evolution does,
using Camel's public C API with OAuth tokens from EDS. It does not
reimplement IMAP or SMTP.

## Design

- CamelSession subclass that gets OAuth tokens via EOAuth2Services
- Accounts discovered from EDS Mail Account sources
- OAuth only: credentials never leave EDS
- All mail operations go through Camel: folder listing, search, message
  fetch, MIME parsing, attachment decode, MIME composition, SMTP sending
- No imaplib or smtplib, no direct protocol code

## Build

```
cd ~/Projects/evomail
make
```

You need `evolution-data-server-devel` (Camel 3.60 headers and the
`camel-1.2` and `libedataserver-1.2` pkg-config files).

## Install

```
make install
```

This installs `evomail` to `~/.local/bin/evomail`. Make sure
`~/.local/bin` is on your `PATH`.

Or install manually:

```
ln -sf ~/Projects/evomail/evomail ~/.local/bin/evomail
```

## Commands

```
evomail accounts                      List Evolution mail accounts
evomail folders                       List folders for an account
evomail list [--limit N]              List messages (default: 20)
evomail read UID                      Print one message
evomail search QUERY                  Search a folder
evomail mark UID...                   Change message flags
evomail send                          Compose and send a message
evomail --version                     Print version
```

Options:

- `--account ACCOUNT` - email, name, UID, or UID prefix
  (default: first non-local account with SMTP, or `EVOMAIL_ACCOUNT`)
- `--folder FOLDER` - folder name or alias (inbox, sent, drafts, trash,
  junk, all, archive)
- `--json` - print JSON instead of text
- `--limit N` - cap results (0 means all)
- `--no-refresh` - skip folder synchronization
- `--dry-run` - build the message and print a summary without sending
- `--no-save-copy` - do not append a copy to the Sent folder

Examples:

```
evomail accounts
evomail list --unread
evomail read 94293 --save-attachments ~/Downloads
evomail search 'from:github is:unread'
evomail mark 94293 94294 --read
evomail send --to friend@example.com --subject Hello --body-file letter.txt --attach report.pdf --dry-run
```

Search keys: `from:`, `to:`, `subject:`, `body:`, `is:unread`,
`is:read`, `is:flagged`, `is:unflagged`. Plain words match subject,
sender, and body.

## Output

Every command supports `--json` for machine-readable output. The JSON
includes message UID, sender, subject, date, flags, and attachment
metadata.

## Status

Tested against Evolution 3.60.2 / EDS 3.60.2 with two Gmail OAuth
accounts: account discovery, folder trees, message listing and reading,
MIME decoding, attachment saving, and flag marking (seen, flagged,
deleted) synced to the server. SMTP dry-run works. Live send has not
been exercised yet. Try `--dry-run` first.
