# fish completion for evomail

function evomail_completion
    set -l cmd (commandline -opc)

    if test (count $cmd) -eq 1
        set -l commands accounts folders list search read export delete send reply forward forward-as-attachment move copy config
        complete -c evomail -a $cmd -d 'evomail command'
    else
        set -l cmd $cmd[1]
        switch $cmd
            case accounts
                complete -c evomail -a 'accounts' -d 'List Evolution mail accounts' -k --account --json --help
            case folders
                complete -c evomail -a 'folders' -d 'List folders for an account' -k --account --folder --json --help
            case list
                complete -c evomail -a 'list' -d 'List messages' -k --account --folder --limit --unread --no-refresh --json --html --save-attachments --mark-seen --help
            case search
                complete -c evomail -a 'search' -d 'Search a folder' -k --account --folder --limit --no-refresh --json --html --help
            case read
                complete -c evomail -a 'read' -d 'Print one message' -k --account --folder --json --html --save-attachments --mark-seen --help
            case export
                complete -c evomail -a 'export' -d 'Export a raw message to a file' -k --account --folder --output --json --help
            case delete
                complete -c evomail -a 'delete' -d 'Delete and expunge messages' -k --account --folder --json --help
            case send
                complete -c evomail -a 'send' -d 'Compose and send a message' -k --account --to --cc --bcc --subject --body --body-file --attach --dry-run --save-copy --no-save-copy --json --html --edit --draft --signature --signature-file --help
            case reply
                complete -c evomail -a 'reply' -d 'Reply to a message' -k --account --to --cc --bcc --subject --body --body-file --attach --dry-run --save-copy --json --html --edit --draft --signature --signature-file --reply-all --help
            case forward
                complete -c evomail -a 'forward' -d 'Forward a message' -k --account --to --cc --bcc --subject --body --body-file --attach --dry-run --save-copy --json --html --edit --draft --signature --signature-file --help
            case forward-as-attachment
                complete -c evomail -a 'forward-as-attachment' -d 'Forward a message as .eml attachment' -k --account --to --cc --bcc --subject --body --body-file --attach --dry-run --save-copy --json --html --edit --draft --signature --signature-file --help
            case move
                complete -c evomail -a 'move' -d 'Move messages to a folder' -k --account --folder --move-to --json --help
            case copy
                complete -c evomail -a 'copy' -d 'Copy messages to a folder' -k --account --folder --copy-to --json --help
            case config
                complete -c evomail -a 'config' -d 'Configure evomail settings' -k --json --help
            case config-set-account
                complete -c evomail -a 'config-set-account' -d 'Set default account' -k muhammedanaspaika@gmail.com muhammedanastech@gmail.com
            case config-set-folder
                complete -c evomail -a 'config-set-folder' -d 'Set default folder' -k inbox sent drafts trash junk sent all archive
        end
    end
end

complete -c evomail -f -c 'evomail_completion'