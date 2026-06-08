# Live Frontend

Run the C++ live server from the project root:

```sh
cmake --build build --target live_order_book_server
./build/live_order_book_server
```

Then open:

```text
http://localhost:8080
```

The browser listens to `/events` for live backend snapshots and sends commands to `/api/control` and `/api/order`.
