# Running a SeventhSlide server

A SeventhSlide server is a computer that is always on and keeps a copy of an
organization's synced libraries. Because the server is always available, the
computers that use a library, such as the main PC, a laptop at home and a
volunteer's computer, can sync with each other through it without ever being
switched on at the same time.

The server is SeventhSlide itself, started with `seventhslide --server`. On
Windows it is a separate program, `seventhslide-server`, installed beside
`seventhslide.exe`. On Windows, wherever this guide says
`seventhslide --server ...`, run `seventhslide-server ...` instead.

The server opens no window and needs no screen, so it can run on a small
computer such as a Raspberry Pi, a NAS or an old PC under a desk. It keeps a
complete copy of each library it holds: the database and every media file.

Setting up a server is a one-time job for someone who is comfortable with a
terminal and with the organization's router. Everyone else only pastes codes.

## Editors and viewers

Every computer in a library has a role. An editor can change anything in the
library. A viewer only receives the library and never writes to it.

Make the server a viewer in every library it holds. Because a viewer writes
nothing to a library, someone who breaks into the server cannot use it to
change the libraries. If you want to be able to do admin work on the server,
also give it the locked admin key (see
[Admin tasks on the server](#admin-tasks-on-the-server)).

## Requirements

- A computer that stays on, either on the local network or anywhere with a
  steady internet connection.
- Enough disk space for the libraries' media (videos above all), plus a little
  more. `status` shows how much space each library takes and how much is free,
  and the log reports when the disk is nearly full.
- Memory: about 2 GB is comfortable. A Raspberry Pi 4 or 5 with 2 GB or more
  works well. One with 1 GB is suitable only for small libraries.
- For computers outside the local network to reach the server, one of the
  following: a port forwarded on the router, IPv6 allowed through the router's
  firewall, or an overlay network such as Tailscale (see
  [Reaching the server from outside the local network](#reaching-the-server-from-outside-the-local-network)).

## Installing

There is no separate server package. Install SeventhSlide on the server
computer as you would anywhere else:

- On Linux, install the deb or rpm package, or build SeventhSlide and install
  it with `cmake --install`.
- On macOS, install it from the release's dmg.
- On Windows, install it for all users.

The sections on running the server as a service, below, give the details for
each system. To build SeventhSlide yourself, see docs/BUILDING.md.

## Starting the server

```text
seventhslide --server [--data DIR] [--port N] [--address HOST[:PORT]]... [--name NAME] [--no-discovery]
```

The server writes what it does to the terminal, or to the system log when it
runs as a service: computers connecting, libraries synced, and anything it
refuses.

Only one server can run with a given data folder at a time.

To keep the server running across restarts, run it as a service. See
[Linux: as a service](#linux-as-a-service),
[macOS: as a service](#macos-as-a-service) and [Windows](#windows).

### Command-line options

- `--data DIR`: The server's own folder, which holds its key and the libraries
  it keeps. Back this folder up. If you leave out `--data`, the server and its
  commands use the folder named in `/etc/seventhslide/server.json`
  (`%ProgramData%\SeventhSlide\server.json` on Windows), which the service setup
  below creates. If that file does not exist, they use a folder named `server`
  inside SeventhSlide's usual data folder. The server refuses to use the data
  folder of a SeventhSlide that is used at the screen.
- `--port N`: The port the server listens on. The default is 8766.
- `--address HOST[:PORT]`: An address at which computers outside the local
  network can reach the server: the organization's internet name (such as
  `slides.example.org`) or its internet address. Give the option once for each
  way in, and include the port if the router forwards a different one. The
  server puts these addresses on each library's member list and in every invite.
  Without `--address`, the server gives out its local addresses, and also its
  IPv6 internet addresses if it has any (see [IPv6](#ipv6)).
- `--name NAME`: The name other computers show for the server, such as "Main
  server". The default is the computer's name.
- `--no-discovery`: The server neither announces itself on the local network nor
  listens for other computers' announcements. Computers can then reach it only
  at its address.

`--port`, `--address`, `--name` and `--no-discovery` override the settings in
the data folder's `sync_settings.json` for that run only. These are the same
settings that the Sync page keeps in SeventhSlide's window. The options are
never written back to the file, and the commands below do not accept them. To
change these settings permanently, use the settings screen or the `settings`
command.

### The local network and the firewall

On the local network, the server and the other computers find each other
automatically by announcing themselves over UDP port 8766. If the server runs a
firewall, it must let in both TCP and UDP on port 8766. On Fedora, for example:

```text
sudo firewall-cmd --permanent --add-port=8766/tcp --add-port=8766/udp && sudo firewall-cmd --reload
```

The router only needs to forward TCP. Announcements never leave the local
network.

## The settings screen

```text
seventhslide --server config
```

The settings screen puts everything in this guide on one screen in the
terminal, in the same words as the Sync page in SeventhSlide's window. It has
three pages:

- **Status**, with **Sync Now**.
- **Libraries**: joining a library and, for each library, its computers,
  invites, roles, the admin key, the size limit, the admin password and recovery
  code, and leaving the library.
- **Settings**: the server's name, addresses and port, the upload speed cap,
  automatic syncing, announcing on the network, and sharing its address.

Use the arrow keys or Tab to move and Enter to choose. Press `1`, `2` or `3` to
change page and `q` to quit. Esc leaves a field.

You can change settings while the server is stopped, but work on libraries
needs the server running. The screen asks for a password each time it needs one
and never keeps it. An invite or a recovery code is shown on a page of its own
so that you can copy it with the terminal's selection. Alternatively, tick
**Print it when I leave, to copy it whole** to have it printed in full when you
quit the screen.

The settings screen needs a terminal. For scripts, containers and services,
each part of it is also available as a command.

## The commands

All of these commands talk to the running server, except `settings`, which
works whether the server is running or not. Run them as the user that owns the
data folder. If you run one as a different user, it says so and shows the
command to run instead. As root, you can run every command except `config` and
`settings`.

| Command | What it does |
|---|---|
| `seventhslide --server config` | Opens the settings screen, which does all of the following on one screen |
| `seventhslide --server settings [NAME VALUE] [--json]` | Shows the sync settings, or changes one of them: `name`, `addresses`, `port`, `discovery`, `sync`, `upload-cap` or `sharing` |
| `seventhslide --server join [CODE]` | Joins a library with an invite, and asks for the code if you don't give it |
| `seventhslide --server status [--json]` | Shows the libraries the server holds, their computers and the disk space |
| `seventhslide --server sync` | Syncs every library now |
| `seventhslide --server admin LIB ... [--yes]` | Does admin work on a library, using its admin password |
| `seventhslide --server address LIB COMPUTER\|INVITE [HOST[:PORT]...\|none]` | Sets where this server reaches a computer of the library, or the computer that an invite is waiting for |
| `seventhslide --server leave LIB [--yes]` | Stops holding a library |
| `seventhslide --server help` | Lists all of the above |

A change made with `settings` takes effect at once where it can, and otherwise
when the server next starts.

`LIB` is a library's id as `status` shows it, or enough of its beginning to tell
it apart from the others. `--yes` answers yes to the question that `leave` and
`admin LIB remove` ask before they act. `--json` gives the same information as
the text output, in JSON. Both options are meant for scripts.

If the server's data folder is not the usual one, add `--data DIR` to any
command.

## Adding a library to the server

The server joins a library in the same way as any other computer: with an
invite. Each library needs its own invite.

1. On an editor computer of the library, open Settings, go to the Sync page,
   click **Add** and choose **Invite a Viewer…**. The invite waits in the list of
   computers until the server joins.

   If the server cannot reach any computer of the library (for example, the
   server is a cloud machine and the editors are at home), select the invite,
   click **Edit…** and add the server's address. The editor computer then
   reaches out to the server for as long as the invite lasts.
2. On the server, run `seventhslide --server join` and paste the code when
   asked.

   For a script, you can give the code on the command line instead
   (`join CODE`). However, other users of that computer can then see the code,
   and it is saved in the shell's history.
3. Optionally, to be able to do admin work on the server, select the server in
   the list of computers on the editor computer, click **Edit…** and tick
   **Holds the admin key**. If you later untick it, the copy of the key that the
   server already has stays there until the admin password is changed.

The invite's secret is in the code and nowhere else. Share the code only with
the person setting up the server, and make a new invite if anyone else saw it.
You can withdraw an invite that has not been used yet: select it and click the
bin icon, or run `admin LIB withdraw INVITE`. Changing the admin password
cancels every invite made before the change.

## Checking status

```text
$ seventhslide --server status
Main server (SeventhSlide 0.1.0), up since 2026-10-25 09:00
  Key 80d2 11b1 822c 6df1; listening on port 8766
  Computers reach it at: slides.example.org:8766
  Data: /var/lib/seventhslide-server, 231.2 GB free of 502.4 GB

Library 3e0a5a234b1b02ada3d57e4c898b7d05
  Up to date. Last synced 2026-10-25 10:02.
  1.2 GB of media, 412 change file(s).
  Computers:
    Main PC             editor  connected now
    Laptop              editor  last heard 2026-10-24 21:13
    Stage display       viewer  not heard from here
    Main server         viewer+key  this server

Connected now: Main PC.
```

In the list of computers, `viewer+key` marks a viewer that holds the locked
admin key.

When something is wrong with a library, `status` reports it under the library's
id, on a line that starts with `Problem:`. Problems include the following:

- The server is not on the library's member list, or has been removed from it.
- The library was changed by a newer version of SeventhSlide: "This library was
  changed by a newer version of SeventhSlide. Update SeventhSlide on this
  computer to keep it in sync."
- The server is "waiting on changes from" a computer. Those changes arrive once
  a computer with this version of SeventhSlide or later sends a copy of the
  whole library.

After the libraries, `status` lists the SeventhSlide computers it has recently
heard on the network. This is a quick way to check that the local network lets
them find each other.

## Admin tasks on the server

Once the server holds the locked admin key, it can do all the admin work that
the Sync page does with the admin password. This helps when the editor
computers are far away.

```text
seventhslide --server admin LIB                         list the library's computers
seventhslide --server admin LIB invite editor           make an invite (valid 7 days; --days N, up to 30)
seventhslide --server admin LIB invite viewer
seventhslide --server admin LIB invite viewer --at far.example.org
                                                        ... and reach out to the joining computer there
seventhslide --server admin LIB withdraw 3f9c           withdraw a waiting invite, so it lets no one in
seventhslide --server admin LIB role "Laptop" viewer    change a computer's role
seventhslide --server admin LIB key "Bridge" on         give a viewer the admin key (off takes it away)
seventhslide --server admin LIB remove "Old laptop"     remove a computer permanently
seventhslide --server admin LIB limit 50GB              set the size limit (or none)
seventhslide --server admin LIB password                set a new admin password
seventhslide --server admin LIB password --recovery     ... using the recovery code
seventhslide --server admin LIB recovery                make a new recovery code
```

Each change asks for the library's admin password (or the recovery code), which
is not shown as you type it. The server forgets it after that one change and
never stores or logs it. Name a computer by its name or its id, as the list
shows them.

## Leaving a library

`seventhslide --server leave LIB` stops the server holding the library and moves
its copy into the `left` folder inside the data folder. Delete it from there
when you no longer need it.

Also take the server off the library's member list, either with
`admin LIB remove "Main server"` or on an editor computer. Otherwise the
library's computers keep looking for it.

## Reaching the server from outside the local network

Computers at home can reach the server over the internet in one of three ways:
port forwarding, IPv6, or an overlay network such as Tailscale.

All traffic between the computers and the server is encrypted, and each side
proves that it holds its key, so you don't need a certificate or a domain. The
server lets in only the computers on a library's member list and a computer
that holds an invite, and it checks every file it is sent.

### Port forwarding

1. Give the server a fixed address on the local network, for example with a
   DHCP reservation on the router.
2. In the router's settings, forward TCP port 8766 to that address.
3. If the organization's internet address never changes, give it to the server
   with `--address`. If it changes, as most do, get a free dynamic DNS name
   (from DuckDNS, No-IP or the router's own service) and give that name with
   `--address` instead.

### IPv6

With IPv6, the server has an internet address of its own, and nothing needs to
be forwarded. On the router, allow incoming TCP port 8766 to the server's IPv6
address. On OpenWrt, this is a traffic rule from `wan` to `lan`. Only computers
that have IPv6 can reach the server this way.

If you give no `--address`, the server gives out the long-lived global IPv6
addresses it finds, leaving out the temporary ones that a computer makes for
privacy. An internet provider may still change these addresses, so a dynamic
DNS name with both an AAAA and an A record is the better choice.

### Tailscale or another overlay network

An overlay network such as Tailscale puts the server and the computers on one
private network, with nothing opened on the router. Install it on the server
and on each computer, and give the server its Tailscale name or address with
`--address`.

### Reaching a SeventhSlide that is not a server

You can make an ordinary SeventhSlide, the one with a window, reachable in the
same ways. Forward the port, and on its Sync page fill in **Routable address**,
and **Port** if the router forwards a different one. SeventhSlide then has to be
left running. A computer that faces the internet should be a viewer in every
library, because a stolen editor key can be used to change the library.

### Upload speed cap

The **Upload speed cap** setting limits the media that a computer sends outside
its own network to the given number of megabits per second, across all of its
connections. This leaves room on the uplink for, say, a livestream. Set it on
the Sync page, on the settings screen, or with `settings upload-cap 5`.

The cap does not apply to computers on the same network. A computer counts as
being on the same network when it has been heard announcing itself there, so
with announcing turned off, the cap applies to every computer.

### Introductions between computers

Computers that sync through the server may be introduced to each other, so that
a laptop at home and the church PC sync directly when they can. To turn this
off for a computer, turn off **Share this computer’s address** on the Sync page
or on the settings screen, or run `settings sharing off`.

## Linux: as a service

`packaging/server/seventhslide-server.service` is a systemd unit that runs the
server as a user of its own, starts it at boot and restarts it if it fails. The
packages do not install it. Copy it, and the `server.json` beside it, from the
`packaging/server` folder of the repository.

The unit's `ExecStart` line names `/usr/local/bin/seventhslide`, which is where
`cmake --install` puts the program. If you installed a deb or rpm package,
change it to `/usr/bin/seventhslide`.

Then create the user, install the unit and start it:

```text
sudo useradd --system --home-dir /var/lib/seventhslide-server --shell /usr/sbin/nologin seventhslide
sudo cp seventhslide-server.service /etc/systemd/system/
sudo mkdir -p /etc/seventhslide
sudo cp server.json /etc/seventhslide/             # tells the commands where the data is
sudo systemctl enable --now seventhslide-server
journalctl -u seventhslide-server -f               # follow the server's log
sudo -u seventhslide seventhslide --server config  # open the settings screen as that user
```

Run the settings screen and the commands as the `seventhslide` user, as in the
last line. They find the data folder on their own. If you run them as root, the
settings screen and `settings` refuse and show the command to use instead,
because files that root writes in the data folder would stop the service.

The unit sets `ProtectHome=yes`, so the service cannot read home folders. The
program and its Qt libraries must therefore be installed where every user can
read them, as a package installs them. The SQLite driver and TLS support come
as Qt plugins. If either is missing, the server refuses to start, and says
which one is missing and where it looked.

## macOS: as a service

`packaging/server/com.seventhslide.server.plist` is a launchd job that runs the
server as a user of its own. launchd starts it at startup, whether or not
anyone is signed in, and starts it again if it stops on its own. The dmg does
not include it. Take it, `server.json` and `packaging/macos/seventhslide` from
the repository.

1. Install SeventhSlide into /Applications from the release's
   `SeventhSlide-macOS-arm64.dmg`, or build it yourself (see docs/BUILDING.md,
   "macOS").
2. SeventhSlide is not signed with a Developer ID, so open it once yourself
   before launchd runs it: Control-click the app, choose **Open**, and then
   choose **Open** again.
3. Create the user and the folders, and install the job:

   ```text
   sudo sysadminctl -addUser _seventhslide -fullName "SeventhSlide server" -UID 450 -roleAccount
   sudo mkdir -p "/Library/Application Support/SeventhSlide Server" /Library/Logs/SeventhSlide
   sudo chown _seventhslide "/Library/Application Support/SeventhSlide Server" /Library/Logs/SeventhSlide
   sudo chmod 700 "/Library/Application Support/SeventhSlide Server"
   sudo mkdir -p /etc/seventhslide
   sudo cp server.json /etc/seventhslide/      # then set "data", as in step 4
   sudo cp com.seventhslide.server.plist /Library/LaunchDaemons/
   sudo launchctl bootstrap system /Library/LaunchDaemons/com.seventhslide.server.plist
   tail -f /Library/Logs/SeventhSlide/server.log                # follow the server's log
   ```

4. In `/etc/seventhslide/server.json`, set `"data"` to
   `/Library/Application Support/SeventhSlide Server` so that the commands find
   the data folder. The data folder is not in a home folder because macOS keeps
   programs that launchd runs out of Documents, Desktop and Downloads.
5. The commands refer to the program as `seventhslide`, but on a Mac the program
   is inside the app. `packaging/macos/seventhslide` puts that name on the PATH.
   Install it, and then open the settings screen as the server's user:

   ```text
   sudo cp seventhslide /usr/local/bin/
   sudo -u _seventhslide seventhslide --server config
   ```

To stop the server, and to start it again (after updating the app, for
example), run:

```text
sudo launchctl bootout system/com.seventhslide.server
sudo launchctl bootstrap system /Library/LaunchDaemons/com.seventhslide.server.plist
```

launchd waits up to two minutes for the server to stop (`ExitTimeOut`), so that
a library that is being applied can finish first.

If the macOS firewall is on (System Settings > Network > Firewall), let the
server through:

```text
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add /Applications/SeventhSlide.app
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --unblockapp /Applications/SeventhSlide.app
```

## Windows

Install SeventhSlide for all users, so that it is in
`C:\Program Files\SeventhSlide`, where the server's account can run it. Then
use Task Scheduler to run the server at startup, whether or not anyone is
signed in:

1. In Task Scheduler, choose **Create Task**. Name the task
   "SeventhSlide server". Choose **Run whether user is logged on or not**, with
   a Windows account made for the server.
2. Under **Triggers**, add a trigger that begins the task At startup.
3. Under **Actions**, add the action Start a program, with the program
   `C:\Program Files\SeventhSlide\seventhslide-server.exe` and the arguments
   `--data C:\SeventhSlideServer`.
4. Under **Settings**, untick "Stop the task if it runs longer than" and tick
   "If the task fails, restart every 1 minute".

To let the server through Windows Firewall, run these from a Command Prompt
opened as administrator:

```text
netsh advfirewall firewall add rule name="SeventhSlide server (TCP)" dir=in action=allow protocol=TCP localport=8766
netsh advfirewall firewall add rule name="SeventhSlide server (UDP)" dir=in action=allow protocol=UDP localport=8766
```

Run the commands (`seventhslide-server status` and the others) while signed in
as that same account, with the same `--data`. Alternatively, put
`{"data": "C:\\SeventhSlideServer"}` in `%ProgramData%\SeventhSlide\server.json`
so that the commands find the folder without it.

## Moving or restoring a server

Everything that makes up the server is in its data folder. To move the server
to another computer:

1. Stop the server.
2. Copy the data folder to the new computer.
3. Start the server there, with the folder as `--data`.

It is the same server, with the same key and libraries. If its address has
changed, update the router's port forwarding.

If a server's data folder is lost, the replacement is a new server. Invite it
to each library again, and remove the old server from each library's member
list.

### Changing the server's address

Computers learn a new address from the member list once they connect. A name
that you keep is therefore the easiest way to reach the server: when the
internet address changes, only the name has to be pointed at the new one.

To change the name or the port, give the old and the new address together
(`--address old --address new`) until `status` shows that every computer has
been heard from since. Then drop the old one.

A computer that was away for that whole time can be given the new address by
hand. On its Sync page, select the server, click **Edit…** and add the address.
If that computer is another server, run
`seventhslide --server address LIB COMPUTER HOST[:PORT]...` on it instead, or
use `none` to clear the addresses again.

## Updating

To update the server, stop it, install the new version of SeventhSlide, and
start it again.

If a release changes how computers talk to each other, every computer of a
library must be updated. Until then, they refuse to sync with each other and
say which one to update. If a library was changed by a newer SeventhSlide than
the server's, the server still keeps the library and passes it on, and `status`
says to update the server.
