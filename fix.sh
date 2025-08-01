#!/bin/bash

git filter-branch -f --env-filter 'if [ "$GIT_AUTHOR_EMAIL" = "thaispinheiro1803@gmail.com" ]; then
     GIT_AUTHOR_EMAIL=wjuan.06@gmail.com;
     GIT_AUTHOR_NAME="Waldomiro Seabra";
     GIT_COMMITTER_EMAIL=$GIT_AUTHOR_EMAIL;
     GIT_COMMITTER_NAME="$GIT_AUTHOR_NAME"; fi' -- --all
