# ACH
Asio Coroutine Http

I have a project, which use asio coroutine for tcp and udp, and i also want to some http and websockets function, but other library is old, api is outdate for asio, and seems dont support asio coroutine, so I start this project. 

## Support
- Router: Directly map, with no other way. So only support fixed path.
- Jwt: have data to manually use jwt test.
- Cors: offer api to process cors.

## Warning
- The coding is mainly generater by ai.
- Temp no test and memory safe check.
- Websockets seems cant use.
- Performance: maybe fast because depend on the one of the best net library and httpparser, but ACH dont do anything to make sure the performance, so actully cant to have high performance. And also have no ability to compare to the profession http server.

## Depend
- asio standalone: https://github.com/chriskohlhoff/asio
- picohttpparser: https://github.com/h2o/picohttpparser
