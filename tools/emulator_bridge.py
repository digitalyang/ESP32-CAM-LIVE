"""Development-only TCP bridge from Android Emulator ports to ESP32-CAM."""

import asyncio
from contextlib import suppress


CAMERA_HOST = "192.168.4.1"
BRIDGES = ((18080, 80), (18181, 81))


async def copy_stream(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
    while data := await reader.read(64 * 1024):
        writer.write(data)
        await writer.drain()


async def relay(
    client_reader: asyncio.StreamReader,
    client_writer: asyncio.StreamWriter,
    remote_port: int,
) -> None:
    try:
        remote_reader, remote_writer = await asyncio.wait_for(
            asyncio.open_connection(CAMERA_HOST, remote_port), timeout=3
        )
    except (OSError, asyncio.TimeoutError):
        client_writer.close()
        await client_writer.wait_closed()
        return

    tasks = {
        asyncio.create_task(copy_stream(client_reader, remote_writer)),
        asyncio.create_task(copy_stream(remote_reader, client_writer)),
    }
    try:
        _, pending = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
        for task in pending:
            task.cancel()
        for task in tasks:
            with suppress(asyncio.CancelledError, ConnectionError, OSError):
                await task
    finally:
        remote_writer.close()
        client_writer.close()
        with suppress(ConnectionError, OSError):
            await remote_writer.wait_closed()
        with suppress(ConnectionError, OSError):
            await client_writer.wait_closed()


async def main() -> None:
    servers = []
    for local_port, remote_port in BRIDGES:
        async def handle(reader, writer, port=remote_port):
            await relay(reader, writer, port)

        servers.append(await asyncio.start_server(handle, "0.0.0.0", local_port))

    await asyncio.gather(*(server.serve_forever() for server in servers))


if __name__ == "__main__":
    asyncio.run(main())
