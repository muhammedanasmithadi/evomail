# evomail — a correct CLI for GNOME Evolution

`evomail` reads mail through Evolution's own account configuration and
mail stack. It does not reimplement IMAP or SMTP; it talks to Evolution
Data Server (EDS) the same way Evolution does, using Camel's public C
API with OAuth tokens retrieved through EDS.

## Design

- **Native stack:** `CamelSession` subclass with
  `get_oauth2_access_token_sync` implemented via `EOAuth2Services`, bound
  to EDS sources with `e_source_camel_configure_service()`.
- **Accounts discovered from EDS:** enabled `Mail Account` sources are
  matched to `Mail Transport` (SMTP) sources by parent UID and email.
- **OAuth only:** credentials never leave EDS; tokens come from
  `get_access_token_sync` and are passed to Camel for XOAUTH2 IMAP/SMTP.
- **Camel for everything:** folder listing, search, message fetch, MIME
  parsing, attachment decode, MIME composition, and SMTP sending all go
  through Camel. No `imaplib`/`smtplib`, no direct protocol code.
- **Production C:** idiomatic GLib/C with `GError` error reporting,
  const-correct APIs, and deterministic resource cleanup.

## Build

```
cd ~/Projects/evomail
make
```

Requires `evolution-data-server-devel` (Camel 3.60 headers and
pkg-config files `camel-1.2`, `libedataserver-1.2`).

## Install

```
make install                  # installs to ~/.local/bin/evomail
# or manually:
ln -sf ~/Projects/evomail/evomail ~/.local/bin/evomail
```

Make sure `~/.local/bin` is on your `PATH`.

## Commands

```
evomail accounts                    List configured Evolution mail accounts
evomail folders                     List folders for an account
evomail list [--limit N]            List messages (default: newest 20)
evomail read UID [--save-attachments DIR] [--mark-seen] [--html]  Print one message
evomail search QUERY                Search a folder
evomail mark UID... --read|--unread|--flag|--unflag|--delete|--undelete [--expunge]
evomail send --to ... [--subject ...] [--body ...|--body-file ...] [--attach ...]
evomail --version|version           Print version
```

Options apply to most commands:

- `--account ACCOUNT` — email, name, UID, or UID prefix (default: first
  non-local account with SMTP, or `EVOMAIL_ACCOUNT`)
- `--folder FOLDER` — folder name or alias (`inbox`, `sent`, `drafts`,
  `trash`, `junk`, `all`, `archive`)
- `--json` — JSON output instead of text
- `--limit N` — cap results (0 = all)
- `--no-refresh` — skip folder synchronization

### Examples

```
evomail accounts
evomail list --unread
evomail read 94293 --save-attachments ~/Downloads
evomail search 'from:github is:unread'
evomail mark 94293 94294 --read
evomail send --to friend@example.com --subject Hello \
    --body-file letter.txt --attach report.pdf --dry-run
```

Search keys: `from:`, `to:`, `subject:`, `body:`, `is:unread`,
`is:read`, `is:flagged`, `is:unflagged`. Plain words match subject,
sender, and body.

`--dry-run` builds and prints the message without sending. `--no-save-copy`
skips appending a copy to the Sent folder.

## Output

All commands support `--json` for machine-readable output. JSON schemas
are consistent across commands and include flags (`seen`, `flagged`),
dates (unix timestamp and ISO-8601), and attachment metadata.

## Status

Validated against Evolution 3.60.2 / EDS 3.60.2 with two Gmail OAuth
accounts: account discovery, folder trees, message listing/read, MIME
decoding, attachment save, flag marking (seen/flagged/deleted) synced to
the server, and SMTP dry-run. Live send has not been exercised by the
developer; use `--dry-run` first.
