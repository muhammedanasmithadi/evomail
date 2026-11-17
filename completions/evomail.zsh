# zsh completion for evomail

_evomail() {
    local cur context state line
    _arguments -s \
        'accounts:list Evolution mail accounts' \
        'folders:List folders for an account' \
        'list:List messages' \
        'search:Search a folder' \
        'read:Print one message' \
        'export:Export a raw message to a file' \
        'delete:Delete and expunge messages' \
        'send:Compose and send a message' \
        'reply:Reply to a message' \
        'forward:Forward a message' \
        'forward-as-attachment:Forward a message as .eml attachment' \
        'move:Move messages to a folder' \
        'copy:Copy messages to a folder' \
        'config:Configure evomail settings' \
        '--version:Print version and exit' \
        '--help:Show help options' \
        '--account:Account email, name, UID, or UID prefix:' \
        '--folder:Folder name or alias:' \
        '--json:Print JSON instead of text' \
        '--limit:Maximum messages to show' \
        '--no-refresh:Do not synchronize the folder before listing' \
        '--unread:Only unread messages' \
        '--html:Show HTML body' \
        '--save-attachments:Save attachments to directory:' \
        '--mark-seen:Mark message as seen' \
        '--to:To recipients:' \
        '--cc:Cc recipients:' \
        '--bcc:Bcc recipients:' \
        '--subject:Subject:' \
        '--body:Message body:' \
        '--body-file:Read body from file:' \
        '--edit:Compose in editor:' \
        '--draft:Save to Drafts folder:' \
        '--signature:Signature text:' \
        '--signature-file:Signature file:' \
        '--attach:Attach file:' \
        '--reply-all:Reply to all:' \
        '--move-to:Move to folder:' \
        '--copy-to:Copy to folder:' \
    && return 0

    case "$state" in
        command)
            _values 'evomail commands' \
                'accounts:List Evolution mail accounts' \
                'folders:List folders for an account' \
                'list:List messages' \
                'search:Search a folder' \
                'read:Print one message' \
                'export:Export a raw message to a file' \
                'delete:Delete and expunge messages' \
                'send:Compose and send a message' \
                'reply:Reply to a message' \
                'forward:Forward a message' \
                'forward-as-attachment:Forward a message as .eml attachment' \
                'move:Move messages to a folder' \
                'copy:Copy messages to a folder' \
                'config:Configure evomail settings' \
                && return 0
            ;;
    esac
}

compdef _evomail evomail