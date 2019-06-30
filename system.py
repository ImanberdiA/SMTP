#!/usr/bin/env python3
# encoding: utf-8

from pathlib import Path
from contextlib import ExitStack
from textwrap import dedent
from tempfile import NamedTemporaryFile, TemporaryDirectory
import subprocess
from subprocess import Popen
from time import sleep
import os
import sys
import signal
from guerrillamail import GuerrillaMailSession
from bs4 import BeautifulSoup

with ExitStack() as exit_stack:
    session = GuerrillaMailSession()
    address = session.get_session_state()['email_address']

    maildir = Path(exit_stack.enter_context(TemporaryDirectory()))

    client = Popen(['valgrind', '--error-exitcode=2', './client'], env={
        'SMTP_MAILDIR': str(maildir)
    })
    exit_stack.callback(client.kill)

    (maildir / 'out').mkdir(parents=True)

    original_subject = 'gfjei734ha7hrlaf2'
    original_message = dedent('''
        X-Original-From: anne@example.com
        X-Original-To: Fer.Sr@yandex.ru
        From: Anne Person <anne@example.com>
        To: Bob Person <Fer.Sr@yandex.ru>
        Subject: gfjei734ha7hrlaf2
        
        This is a test message.

        Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed egestas
        mollis nunc vitae lobortis. Curabitur vel tincidunt massa, quis
        hendrerit nunc. Nam a magna felis. Praesent convallis tincidunt magna,
        id tempor dui vestibulum sed. Nam blandit, lacus quis elementum
        vehicula, orci leo tincidunt mi, aliquet pharetra odio ante ac lectus.
        Vivamus aliquam augue velit, mattis iaculis quam elementum sit amet.
        
        Nulla venenatis lacus mi, sed tincidunt sapien consectetur sed. Nam sit
        amet nisi varius, posuere dolor in, elementum velit. Quisque sed
        ullamcorper metus, sit amet rutrum augue. Curabitur tincidunt convallis
        nisi, molestie feugiat quam placerat non. Nullam at ex id nunc iaculis
        rutrum et facilisis nunc. Donec luctus leo at sem blandit facilisis.
        Mauris et velit nec lorem vestibulum faucibus quis aliquam nunc.
        Nunc et ante ultricies justo maximus pulvinar.
    ''').lstrip()
    original_body = original_message.split('\n\n', 1)[1].rstrip()
    original_message = original_message.replace('\n', '\r\n')

    with NamedTemporaryFile('wb', delete=False) as message_file:
        message_path = Path(message_file.name)
        message_file.write(original_message.encode('UTF-8'))

    message_path.rename(maildir / 'out' / message_path.name)

    while True:
        for email in session.get_email_list():
            if email.subject == original_subject:
                break
        else:
            sleep(1)
            continue
        break

    client.send_signal(signal.SIGINT)
    try:
        client.wait(10)
    except subprocess.TimeoutExpired:
        print('client termination timed out', file=sys.stderr)
        sys.exit(1)
    if client.returncode == 2:
        print('valgrind detected memory errors', file=sys.stderr)
        sys.exit(1)
    if client.returncode != 0:
        print('client terminated with error', file=sys.stderr)
        sys.exit(1)

    email = session.get_email(email.guid)
    recovered_body = email.body
    recovered_body = BeautifulSoup(recovered_body, 'html.parser').get_text()

    if recovered_body != original_body:
        print('recovered message doesn\'t match original', file=sys.stderr)
        sys.exit(1)

    print('message transfer completed successfully', file=sys.stderr)

