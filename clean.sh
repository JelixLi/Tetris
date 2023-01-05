s=($(docker ps | grep serving))
if [ "${s[0]}" = "" ]
then
  echo "no instance found!"
else  
  docker stop ${s[0]}
  echo "instance stopped!"
fi