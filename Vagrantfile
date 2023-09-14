# -*- mode: ruby -*-
# vi: set ft=ruby :

# All Vagrant configuration is done below. The "2" in Vagrant.configure
# configures the configuration version (we support older styles for
# backwards compatibility). Please don't change it unless you know what
# you're doing.
Vagrant.configure("2") do |config|
  config.vm.box = "ubuntu/jammy64"
  config.vm.synced_folder ".", "/vagrant", mount_options: ["fmode=777", "dmode=777"]
  config.vm.boot_timeout = 1200


  # Customize the number of cores and amount of memory on the VM:
  config.vm.provider "virtualbox" do |vb|
    vb.cpus = "2"
    vb.memory = "512"
  end

  # Setup default packages and configuration files
  config.vm.provision "shell", inline: <<-SHELL
    apt-get update
    apt-get install -y build-essential gcc-multilib emacs htop gdb valgrind ntp
    for i in `ls /home`; do echo 'cd /vagrant' >>/home/${i}/.bashrc; done
    for i in `ls /home`; do curl --insecure https://www.cse.psu.edu/~tuz68/.emacs 2>/dev/null >/home/${i}/.emacs; chown ${i}:${i} /home/${i}/.emacs; done
    for i in `ls /home`; do curl --insecure https://www.cse.psu.edu/~tuz68/.vimrc 2>/dev/null >/home/${i}/.vimrc; chown ${i}:${i} /home/${i}/.vimrc; done
  SHELL
end
