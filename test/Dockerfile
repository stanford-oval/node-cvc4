FROM ubuntu:18.04

RUN apt update
RUN apt install -y apt-transport-https curl gnupg2
RUN curl -sS https://dl.yarnpkg.com/debian/pubkey.gpg | apt-key add -
RUN echo "deb https://dl.yarnpkg.com/debian/ stable main" | tee /etc/apt/sources.list.d/yarn.list
RUN curl -sL https://deb.nodesource.com/setup_8.x | bash -
RUN apt -y update
RUN apt -y install cvc4 libcvc4-dev yarn build-essential git

COPY . node-cvc4
RUN cd node-cvc4 ; yarn