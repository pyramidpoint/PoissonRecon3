%fx_right1=textread("D:\qingjiaoyun\PoissonRecon3\Bin\x64\Release\IsoValue.txt");
fx_right1=textread("D:\qingjiaoyun\PoissonRecon_green\Bin\x64\Release\IsoValue_9.0.txt")
fx_right2=textread("D:\qingjiaoyun\PoissonRecon_green\Bin\x64\Release\isovalue9_0modify.txt")


figure
subplot(2,1,1)

for i=1:length(fx_right1)
    if fx_right1(i)==max(fx_right1)
        maxindex=i;
    end
end
fx_right1(maxindex)=0;
plot(fx_right1);
title('9.0版本的iso')
subplot(2,1,2)
plot(fx_right2);
title("修改后的iso")



% x=1:length(fx);
% hold on
% %y=ones(1,length(fx))*(sum(fx)/length(fx));
% y=ones(1,length(fx))*(-83);
% %plot(x,y);
% title(["isovalue is ",num2str(sum(fx)/length(fx))]);
% legend('扰动为小于0.5的随机数')
% ylabel("resultOfSolve")
% fx_0=textread("D:\qingjiaoyun\PoissonRecon\Bin\x64\Release\IsoValue_0.txt");
% fx_1=textread("D:\qingjiaoyun\PoissonRecon\Bin\x64\Release\IsoValue_0.1.txt");
% fx_2=textread("D:\qingjiaoyun\PoissonRecon\Bin\x64\Release\IsoValue_0.2.txt");
% fx_3=textread("D:\qingjiaoyun\PoissonRecon\Bin\x64\Release\IsoValue_0.3.txt");
% fx_5=textread("D:\qingjiaoyun\PoissonRecon\Bin\x64\Release\IsoValue_0.5.txt");
% % fx=[];
% % for i=1:length(fx_right)
% %    if rem(i-1,7)==0
% %        fx=[fx;fx_right(i)];
% %    end
% % end
% figure
% subplot(3,2,1)
% plot(fx_1)
% title(["isovalue is ",num2str(sum(fx_1)/length(fx_1))]);
% legend('扰动为小于0.1的随机数')
% ylabel("resultOfSolve")
% subplot(3,2,2)
% plot(fx_2)
% title(["isovalue is ",num2str(sum(fx_2)/length(fx_2))]);
% legend('扰动为小于0.2的随机数')
% ylabel("resultOfSolve")
% subplot(3,2,3)
% plot(fx_3)
% title(["isovalue is ",num2str(sum(fx_3)/length(fx_3))]);
% legend('扰动为小于0.3的随机数')
% ylabel("resultOfSolve")
% subplot(3,2,4)
% plot(fx_5)
% title(["isovalue is ",num2str(sum(fx_5)/length(fx_5))]);
% legend('扰动为小于0.5的随机数')
% ylabel("resultOfSolve")
% 
% 
% subplot(3,2,[5,6])
% plot(fx_0)
% hold on
% plot(fx_1)
% hold on
% plot(fx_2)
% hold on
% plot(fx_3)
% hold on
% plot(fx_5)
% ylabel("resultOfSolve")
% title("all")
% legend("0","0.1","0.2","0.3","0.5")
% 
