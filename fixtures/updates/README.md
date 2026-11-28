# Update fixtures

`releases.json` is a reply to GitHub's `GET /repos/<owner>/<repo>/releases`,
cut down to a few releases. Each file's `digest` is the SHA-256 of its tag
followed by its name (`v1.3.1seventhslide-ubuntu-24.04-amd64.deb`), so a test
can publish a file that matches it without storing the file itself.
