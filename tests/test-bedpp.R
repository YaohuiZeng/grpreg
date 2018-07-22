## test BEDPP

DF <-read.csv(text=RCurl::getURL("https://raw.githubusercontent.com/jonhersh/datasets/master/myDF2.csv"), header=T)

X <- DF[,1:74]
Y <- DF[,75]
groups <- c(1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,6,6,6,7,7,7,7,7,8,8,8,8,8,8,8,8,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9)
groups.factor <- factor(groups, labels = c("head","hh demographics","dwelling","public services","durables","education","region/sector","head LF","hh LF"))

fit <- grpreg(X, Y, group=groups.factor)
fit.bedpp <- grpreg(X, Y, group=groups.factor, screen = "SSR-BEDPP")
stopifnot(all.equal(fit$loss, fit.bedpp$loss))
stopifnot(all.equal(fit$beta, fit.bedpp$beta))
