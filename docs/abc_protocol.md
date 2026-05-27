我来重新设计一下A B C三者桥接。A B C之前要设计一种新的协议。暂时称他为ABC协议，后续你帮我命名。 它的工作过程是.

1. B主动连接A ，发送一个协议，包含id+role，B进入等待匹配状态
2. C主动连接A，发送一个协议，包含id+role，C进入等待匹配状态
3. A匹配到B、C为一组，分别向B、C发送ready，B、C进入ready状态
4. A任何时候，发现B或C断开，和另一方也断开。

5. B在ready状态后，对外listen一个端口，等待client连接
6. 当有client 1 连接B时，B给这个连接生成一个uuid。
7. B 通知A 有一个新连接，告诉A uuid
8. A 通知 C有一个新连接，告诉C uuid
9. C 和remote建立一个新的tcp连接，关联到这个uuid

8. B把client的数据 包在一个frame里发给A，frame包含uuid+data
9. A转发给C
10. C 根据uuid，用关联的tcp发出去。
11. 从C收到的数据，逆向返回给B，B返回给相应的client
12. B可以同时处理多个client
13. A可以同时处理多组B、C配对

