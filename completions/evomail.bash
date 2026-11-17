# bash completion for evomail

_evomail() {
    local cur prev opts
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    # Top-level commands
    local commands="accounts folders list search read export delete send reply forward forward-as-attachment move copy config --help --version --config-set-account --config-set-folder --config-wizard --config-set-account --config-set-folder"

    if [[ ${COMP_CWORD} -eq 1 ]]; then
        COMPREPLY=( $(compgen -W "${commands}" -- ${cur}) )
        return 0
    fi

    case "${prev}" in
        evomail)
            COMPREPLY=( $(compgen -W "${commands}" -- ${cur}) )
            return 0
            ;;
        --account|-a)
            COMPREPLY=( $(compgen -W "muhammedanaspaika@gmail.com muhammedanastech@gmail.com" -- ${cur}) )
            return 0
            ;;
        --folder|-f)
            COMPREPLY=( $(compgen -W "inbox sent drafts trash junk sent all archive" -- ${cur}) )
            return 0
            ;;
        --to|-t)
            COMPREPLY=( $(compgen -o filenames -- ${cur}) )
            return 0
            ;;
        --body-file|--save-attachments|--output|--attach)
            COMPREPLY=( $(compgen -o filenames -- ${cur}) )
            return 0
            ;;
        --signature-file)
            COMPREPLY=( $(compgen -o filenames -- ${cur}) )
            return 0
            ;;
    esac

    # Second-level commands based on first argument
    case "${COMP_WORDS[1]}" in
        accounts)
            COMPREPLY=( $(compgen -W "--account --json --help" -- ${cur}) )
            return 0
            ;;
        folders)
            COMPREPLY=( $(compgen -W "--account --folder --json --help" -- ${cur}) )
            return 0
            ;;
        list)
            COMPREPLY=( $(compgen -W "--account --folder --limit --unread --no-refresh --json --html --save-attachments --mark-seen --help" -- ${cur}) )
            return 0
            ;;
        search)
            COMPREPLY=( $(compgen -W "--account --folder --limit --no-refresh --json --html --help" -- ${cur}) )
            return 0
            ;;
        read)
            COMPREPLY=( $(compgen -W "--account --folder --json --html --save-attachments --mark-seen --help" -- ${cur}) )
            return 0
            ;;
        mark)
            COMPREPLY=( $(compgen -W "--account --folder --read --unread --flag --unflag --delete --undelete --expunge --json --html --save-attachments --mark-seen --help" -- ${cur}) )
            return 0
            ;;
        export)
            COMPREPLY=( $(compgen -W "--account --folder --output --json --help" -- ${cur}) )
            return 0
            ;;
        delete)
            COMPREPLY=( $(compgen -W "--account --folder --json --help" -- ${cur}) )
            return 0
            ;;
        send)
            COMPREPLY=( $(compgen -W "--account --folder --to --cc --bcc --subject --body --body-file --attach --dry-run --save-copy --no-save-copy --json --html --edit --draft --signature --signature-file --help" -- ${cur}) )
            return 0
            ;;
        reply)
            COMPREPLY=( $(compgen -W "--account --folder --to --cc --bcc --subject --body --body-file --attach --dry-run --save-copy --json --html --edit --draft --signature --signature-file --reply-all --help" -- ${cur}) )
            return 0
            ;;
        forward)
            COMPREPLY=( $(compgen -W "--account --folder --to --cc --bcc --subject --body --body-file --attach --dry-run --save-copy --json --html --edit --draft --signature --signature-file --help" -- ${cur}) )
            return 0
            ;;
        forward-as-attachment)
            COMPREPLY=( $(compgen -W "--account --folder --to --cc --bcc --subject --body --body-file --attach --dry-run --save-copy --json --html --edit --draft --signature --signature-file --help" -- ${cur}) )
            return 0
            ;;
        move)
            COMPREPLY=( $(compgen -W "--account --folder --move-to --json --help" -- ${cur}) )
            return 0
            ;;
        copy)
            COMPREPLY=( $(compgen -W "--account --folder --copy-to --json --help" -- ${cur}) )
            return 0
            ;;
        config)
            COMPREPLY=( $(compgen -W "--json --help" -- ${cur}) )
            return 0
            ;;
        config-set-account|--config-set-account)
            COMPREPLY=( $(compgen -W "muhammedanaspaika@gmail.com muhammedanastech@gmail.com" -- ${cur}) )
            return 0
            ;;
        config-set-folder|--config-set-folder)
            COMPREPLY=( $(compgen -W "inbox sent drafts trash junk sent all archive" -- ${cur}) )
            return 0
            ;;
    esac

    COMPREPLY=()
}

complete -F _evomail evomail