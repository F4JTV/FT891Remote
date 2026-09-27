# Remote Access and Security

## Reaching the station from outside

The server listens on **TCP 7300** (control) and **UDP 7301** (audio). To
reach it from outside your network, forward both ports on your router to the
server machine, and use your public address (or a dynamic DNS name) in the
client.

A **VPN** (WireGuard, Tailscale, OpenVPN…) is the safer alternative: nothing
is exposed to the Internet, and the client uses the server's VPN address.

## Protecting the link

- A **password** is required from every client. The server sends a
  challenge; the password itself never travels.
- **Encryption**: with "Encrypt the link" on in the client, control and audio
  are encrypted with ChaCha20. On the server, "Reject clients that do not
  encrypt" makes it mandatory — turn it on whenever the ports are reachable
  from the Internet.
- **One client at a time** operates the station.

## Protecting the transmitter

- **Dead-man release**: if the client goes silent while transmitting —
  network lost, program closed — the server releases the PTT.
- **Band edges**: the server refuses to transmit outside the amateur bands
  of the IARU region chosen on its Network tab, judging the emitted spectrum
  with split and clarifier.
- **Confirmations**: a reset, a CAT-rate change and a power-off must be
  confirmed in the client.

You remain responsible for your transmissions and for the rules that apply
to remote operation where you and the station are.
