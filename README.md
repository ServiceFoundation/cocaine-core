Почти всё, что написано ниже — устаревшая информация, ждите апдейтов.

Что это вообще такое?
=====================

Кокаин — это лёгкий и шустрый демон, который может следующее:

* Планировать и асинхронно выполнять некие произвольные _задачи_:
  * По относительным интервалам (например, каждую минуту).
  * По времени, которое задача сама себе выбрала.
  * По событиям на файловой системе (новый файл в /var/lib/data).
  * По данным на сокете (что-то пришло на tcp://*:1234).
* Асинхронно и единовременно выполнять эти же _задачи_ (map) [TBD: и собирать их результаты в нескольких местах для агрегации (reduce)] и возвращать результаты их выполнения одним запросом.

Всё общение с демоном построено на [ZeroMQ](https://github.com/zeromq/libzmq), поэтому в качестве транспортов могут быть использованы следующие протоколы: IPC, TCP, [PGM](http://en.wikipedia.org/wiki/Pragmatic_General_Multicast).
Сам демон ничего про задачи не знает - всю полезную нагрузку делают плагины, представляющие собой самые обычные .so модули, которые можно быстро и просто писать на C++ и динамически подгружать. Шаблон такого модуля можно посмотреть [здесь](plugins/plugin-template.cpp).

На данный момент, есть вот такие плагины:

* __MySQL__ - может ходить на сервер, и проверять, жив ли он.
* __HTTP__ - может ходить по урлу и говорить какой там был код возврата.
* __Python__ - позволяет писать плагины на Пейтоне.
* __Javascript__ - позволяет писать плагины на Javascript-е.

От плагина требуется две вещи:

1. Он должен уметь принимать на вход некую специфичную для него строку параметров вида scheme://<какой-нибудь текст> (из этого будет сгенерирован _идентификатор источника_) и возвращать _источник_.
2. Источник должен уметь:
  * Сообщить демону о том, что он умеет делать.
  * Для объявленных способностей иметь реализацию соответствующих методов.

Как это работает
================

Есть два режима работы демона — планирование и MapReduce. Их, естественно, можно использовать вместе и ничего нигде переключать не нужно.

Планирование
------------

1. Клиент локально или по сети говорит демону, что он хочет подписаться на результаты работы такого-то _источника_ (например, ```python://repository/workload.py/WorkloadClass?arg1=foo&arg2=bar```), с такими-то параметрами, который нужно дёргать с помощью таких-то _драйверов_ (всё это вместе называется _задачей_):
 * __auto__: дёргать источник раз n миллисекунд;
 * __manual__: при инициализации и после каждого запуска демон спрашивает у источника, когда его следует запустить в следующий раз;
 * __fs__: дёргать источник при событии на файловой системе по указанному пути;
 * __zeromq__: дёргать источник при событии на указанном сокете, причём полученные данные будут переданы источнику для обработки;
2. Демон создаёт новый _источник_ и запускает его треде (если треда для источника с таким же идентификатором ещё нет или если в запросе указан параметр ```{"isolated": true}```) и возвращает клиенту связанный с _источником_, _драйвером_ ((и идентификатор треда в случае изолированных задач) ключ, по которому можно подписаться либо на все результаты его работы сразу, либо только на отдельные поля (либо вообще забить и не подписываться, если он нужен только ради побочных эффектов). По этому же ключу можно будет запрашивать историю.
3. Демон кладёт описание этой _задачи_ в свой сторадж, чтобы при перезапуске/смерти или ещё каком-нибудь катаклизме можно было бы восстановить все задачи без каких-либо лишних телодвижений (если только в запросе не будет указано ```{"transient": true}```). Дополнительно, демон предоставляет собственный сторадж для _источника_ (или для всех источников с одинаковыми идентификаторами на кластере, если в качестве стораджа используется что-нибудь распределённое, типа MongoDB), чтобы он или они могли туда что-то сложить или забрать.
4. Все остальные клиенты, которые захотят делать что-нибудь с _источником_ с тем же _идентификатором_, но с другим _драйвером_, будут напавлены в тот же тред, если только они не укажут в запросе ```{"isolated": true}```, в случае чего будет создан специальный изолированный тред и новый экземпляр источника конкретно под этот запрос.
5. Если все клиенты для конкретного экземпляра _источника_ скажут, что они больше ничего получать не хотят (другими словами, остановят все _драйверы_), то демон через 10 минут убьёт тред для этого источника.

MapReduce
---------

1. Клиент локально или по сети говорит демонам, что он хочет выполнить вот такую вот задачу с драйвером __once__.
2. Демоны стартуют под это дело тред (если треда ещё нет и задача не изолирована) и в нём эту задачу выполняют.
3. После того, как задача выполнена, тред живёт еще 10 минут в ожидании что кто-то ещё попросит что-нибудь аналогичное, а потом ликвидируется, чтобы не занимать ресурсы.
4. Клиент получает результаты задачи ото всех демонов, после чего может их каким-нибудь образом агрегировать.

Авторизация
-----------

Демон можно сконфигурировать таким образом, что он будет ожидать multi-part сообщения вида ```[json-reqest] [rsa signature]``` вместо простого запроса. Работает это следующим образом: в самом запросе указан токен, на стороне сервера в сторадже лежат публичные ключи для этих токенов. Если публичного ключа для токена не найдено, считается что юзер с этим токеном не имеет права приходить с запросами. Если ключ есть, то подпись будет провалидирована, и, если всё хорошо, запрос будет выполнен.

История
-------

Для каждого _драйвера_ хранится история длинной --history-depth (=10). Чтобы её получить, нужно прийти с запросом вида ```{"action": "past", "targets": {"<subscription key>": {"depth": 5}}}```.

Статистика
----------

Чтобы получить какую-то унылую статистику, можно прийти с запросом ```{"action": "stats"}```.

Примеры
=======

Это very low level пример, написанный на голом Пейтоне, в идеальном мире есть простой и понятный клиент, который скрывает все эти кишки под капотом.

```sh
user@host:~$ sudo apt-get update && sudo apt-get install cocaine-core cocaine-plugin-python
user@host:~$ /usr/bin/cocained tcp://lo:5000 --export tcp://lo:5001 --watermark 100 --verbose --daemonize
```

/usr/lib/cocaine/python.d/file.py:

```python
class MyClass(object):
    def __init__(self, some_arg, another_arg):
        self.some_arg = int(some_arg)
        self.another_arg = str(another_arg)

    def __iter__(self):
        return self

    def next(self):    
        return {'some-field': self.some_arg * 2,
                'another-field': self.another_arg}
```

Инициализиурем ZeroMQ:

```python
>>> import zmq
>>> context = zmq.Context()
>>> socket = ctx.socket(zmq.REQ)
>>> socket.connect('tcp://localhost:5000')
```

Обычный запрос:

```python
>>> socket.send_json({
...     'version': 2,
...     'token': 'username',
...     'action': 'push',
...     'targets': {
...         'python:///file.py/MyClass?some_arg=5&another_arg=abc': {
...             'driver': 'auto',
...             'interval': 5000,
...             'isolated': False,
...             'transient': False
...          }
...     }
... })
>>> response = socket.recv_json()
>>> print response
{'python:///file.py/MyClass?some_arg=5&another_arg=abc': {'key': 'auto:de3ca129f34d...'}}
>>> # Also, the code could be fetched from some remote host
>>> # socket.send_json({..., 'targets': {'python://code.server.com/code/file.py...': {...}}})
```

Просим делать что-нибудь с авторизацией:

```python
>>> import json
>>> from M2Crypto import EVP
>>> pk = EVP.load_key('/path/to/private-key.pem')
>>> pk.sign_init()
>>> request = {'version': 3, ...}
>>> request_json = json.write(request)
>>> pk.sign_update(request_json)
>>> signature = pk.sign_final()
>>> socket.send_multipart([request_json, signature])
>>> response = socket.recv_json()
```

Подписываемся на результаты:

```python
>>> # Get the subscription key
>>> key = response["python:///file.py/MyClass?some_arg=5&another_arg=abc"]["key"]
>>> # Create a subscriber socket
>>> subscriber = context.socket(zmq.SUB)
>>> # To enable ZeroMQ guaranteed delivery feature, set the socket identity
>>> subscriber.setsockopt(zmq.IDENTITY, "ClientName")
>>> # Subscribe to the source
>>> subscriber.setsockopt(zmq.SUBSCRIBE, key)
>>> # Alternatively, subscribe to a set of fields, ignoring all the others
>>> # subscriber.setsockopt(zmq.SUBSCRIBE, "%s another-field" % key)
>>> subscriber.connect('tcp://localhost:5001')
>>> # Receive the results
>>> while True:
>>>     print subscriber.recv_multipart()
```

Делаем once-запрос:

```python
>>> socket.send_json({
...     'version': 2,
...     'token': 'username',
...     'action': 'push',
...     'targets': {
...         'python:///file.py/MyClass?some_arg=5&another_arg=abc': {
...             'driver': 'once',
...             'isolated': True
...          }
...     }
... })
>>> response = socket.recv_json()
>>> print response
{'python:///file.py/MyClass?some_arg=5&another_arg=abc': {'some-field': 10, 'another-field': 'abc'}}
```